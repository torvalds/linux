/**
 * @file definition of host message ring functionality
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
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

#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmmsgbuf.h>
#include <bcmendian.h>
#include <bcmstdlib_s.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>

#ifdef BCMDBUS
#include <dbus.h>
#else
#include <dhd_bus.h>
#endif /* BCMDBUS */

#include <dhd_dbg.h>
#include <siutils.h>
#include <dhd_debug.h>
#ifdef EXT_STA
#include <wlc_cfg.h>
#include <wlc_pub.h>
#include <wl_port_if.h>
#endif /* EXT_STA */

#include <dhd_flowring.h>

#include <pcie_core.h>
#include <bcmpcie.h>
#include <dhd_pcie.h>
#ifdef DHD_TIMESYNC
#include <dhd_timesync.h>
#endif /* DHD_TIMESYNC */
#ifdef DHD_PKTTS
#include <bcmudp.h>
#include <bcmtcp.h>
#endif /* DHD_PKTTS */
#include <dhd_config.h>

#if defined(DHD_LB)
#if !defined(LINUX) && !defined(linux) && !defined(OEM_ANDROID)
#error "DHD Loadbalancing only supported on LINUX | OEM_ANDROID"
#endif /* !LINUX && !OEM_ANDROID */
#include <linux/cpu.h>
#include <bcm_ring.h>
#define DHD_LB_WORKQ_SZ			    (8192)
#define DHD_LB_WORKQ_SYNC           (16)
#define DHD_LB_WORK_SCHED           (DHD_LB_WORKQ_SYNC * 2)
#endif /* DHD_LB */

#include <etd.h>
#include <hnd_debug.h>
#include <bcmtlv.h>
#include <hnd_armtrap.h>
#include <dnglevent.h>

#ifdef DHD_PKT_LOGGING
#include <dhd_pktlog.h>
#include <dhd_linux_pktdump.h>
#endif /* DHD_PKT_LOGGING */
#ifdef DHD_EWPR_VER2
#include <dhd_bitpack.h>
#endif /* DHD_EWPR_VER2 */

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

#if defined(DHD_HTPUT_TUNABLES)
#define DEFAULT_RX_BUFFERS_TO_POST		1024
#define RX_BUF_BURST				64 /* Rx buffers for MSDU Data */
#define RXBUFPOST_THRESHOLD			64 /* Rxbuf post threshold */
#else
#define DEFAULT_RX_BUFFERS_TO_POST		256
#define RX_BUF_BURST				32 /* Rx buffers for MSDU Data */
#define RXBUFPOST_THRESHOLD			32 /* Rxbuf post threshold */
#endif /* DHD_HTPUT_TUNABLES */

/* Read index update Magic sequence */
#define DHD_DMA_INDX_SEQ_H2D_DB_MAGIC	0xDDDDDDDDAu
#define DHD_RDPTR_UPDATE_H2D_DB_MAGIC(ring)	(0xDD000000 | (ring->idx << 16u) | ring->rd)
/* Write index update Magic sequence */
#define DHD_WRPTR_UPDATE_H2D_DB_MAGIC(ring)	(0xFF000000 | (ring->idx << 16u) | ring->wr)
#define DHD_AGGR_H2D_DB_MAGIC	0xFFFFFFFAu

#define DHD_STOP_QUEUE_THRESHOLD	200
#define DHD_START_QUEUE_THRESHOLD	100

#define RX_DMA_OFFSET		8 /* Mem2mem DMA inserts an extra 8 */
#define IOCT_RETBUF_SIZE	(RX_DMA_OFFSET + WLC_IOCTL_MAXLEN)

/* flags for ioctl pending status */
#define MSGBUF_IOCTL_ACK_PENDING	(1<<0)
#define MSGBUF_IOCTL_RESP_PENDING	(1<<1)

#define DHD_IOCTL_REQ_PKTBUFSZ		2048
#define MSGBUF_IOCTL_MAX_RQSTLEN	(DHD_IOCTL_REQ_PKTBUFSZ - H2DRING_CTRL_SUB_ITEMSIZE)

/**
 * XXX: DMA_ALIGN_LEN use is overloaded:
 * - as align bits: in DMA_ALLOC_CONSISTENT 1 << 4
 * - in ensuring that a buffer's va is 4 Byte aligned
 * - in rounding up a buffer length to 4 Bytes.
 */
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
#ifdef BTLOG
#define DHD_H2D_BTLOGRING_MAX_BUF_POST			32
#endif	/* BTLOG */
#define DHD_MAX_TSBUF_POST			8

#define DHD_PROT_FUNCS	43

/* Length of buffer in host for bus throughput measurement */
#define DHD_BUS_TPUT_BUF_LEN 2048

#define TXP_FLUSH_NITEMS

/* optimization to write "n" tx items at a time to ring */
#define TXP_FLUSH_MAX_ITEMS_FLUSH_CNT	48

#define RING_NAME_MAX_LENGTH		24
#define CTRLSUB_HOSTTS_MEESAGE_SIZE		1024
/* Giving room before ioctl_trans_id rollsover. */
#define BUFFER_BEFORE_ROLLOVER 300

/* 512K memory + 32K registers */
#define SNAPSHOT_UPLOAD_BUF_SIZE	((512 + 32) * 1024)

struct msgbuf_ring; /* ring context for common and flow rings */

#ifdef DHD_HMAPTEST
/* 5 * DMA_CONSISTENT_ALIGN as different tests use upto 4th page */
#define HMAP_SANDBOX_BUFFER_LEN	(DMA_CONSISTENT_ALIGN * 5) /* for a 4k page this is 20K */
/**
 * for D11 DMA HMAPTEST thes states are as follows
 * iovar sets ACTIVE state
 * next TXPOST / RXPOST sets POSTED state
 * on TXCPL / RXCPL POSTED + pktid match does buffer free nd state changed to INACTIVE
 * This ensures that on an iovar only one buffer is replaced from sandbox area
 */
#define HMAPTEST_D11_TX_INACTIVE 0
#define HMAPTEST_D11_TX_ACTIVE 1
#define HMAPTEST_D11_TX_POSTED 2

#define HMAPTEST_D11_RX_INACTIVE 0
#define HMAPTEST_D11_RX_ACTIVE 1
#define HMAPTEST_D11_RX_POSTED 2
#endif /* DHD_HMAPTEST */

#define PCIE_DMA_LOOPBACK	0
#define D11_DMA_LOOPBACK	1
#define BMC_DMA_LOOPBACK	2

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

#define PCIE_D2H_SYNC_WAIT_TRIES    (512U)
#define PCIE_D2H_SYNC_NUM_OF_STEPS  (5U)
#define PCIE_D2H_SYNC_DELAY         (100UL)	/* in terms of usecs */

#ifdef DHD_REPLACE_LOG_INFO_TO_TRACE
#define DHD_MSGBUF_INFO DHD_TRACE
#else
#define DHD_MSGBUF_INFO DHD_INFO
#endif /* DHD_REPLACE_LOG_INFO_TO_TRACE */

/**
 * Custom callback attached based upon D2H DMA Sync mode advertized by dongle.
 *
 * On success: return cmn_msg_hdr_t::msg_type
 * On failure: return 0 (invalid msg_type)
 */
typedef uint8 (* d2h_sync_cb_t)(dhd_pub_t *dhd, struct msgbuf_ring *ring,
                                volatile cmn_msg_hdr_t *msg, int msglen);

/**
 * Custom callback attached based upon D2H DMA Sync mode advertized by dongle.
 * For EDL messages.
 *
 * On success: return cmn_msg_hdr_t::msg_type
 * On failure: return 0 (invalid msg_type)
 */
#ifdef EWP_EDL
typedef int (* d2h_edl_sync_cb_t)(dhd_pub_t *dhd, struct msgbuf_ring *ring,
                                volatile cmn_msg_hdr_t *msg);
#endif /* EWP_EDL */

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

/* XXX: The ringid and flowid and dma indices array index idiosyncracy is error
 * prone. While a simplification is possible, the backward compatability
 * requirement (DHD should operate with any PCIE rev version of firmware),
 * limits what may be accomplished.
 *
 * At the minimum, implementation should use macros for any conversions
 * facilitating introduction of future PCIE FD revs that need more "common" or
 * other dynamic rings.
 */

/* XXX: Presently there is no need for maintaining both a dmah and a secdmah */
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

/* Used in loopback tests */
typedef struct dhd_dmaxfer {
	dhd_dma_buf_t srcmem;
	dhd_dma_buf_t dstmem;
	uint32        srcdelay;
	uint32        destdelay;
	uint32        len;
	bool          in_progress;
	uint64        start_usec;
	uint64        time_taken;
	uint32        d11_lpbk;
	int           status;
} dhd_dmaxfer_t;

#ifdef DHD_HMAPTEST
/* Used in HMAP test */
typedef struct dhd_hmaptest {
	dhd_dma_buf_t	mem;
	uint32		len;
	bool	in_progress;
	uint32	is_write;
	uint32	accesstype;
	uint64  start_usec;
	uint32	offset;
} dhd_hmaptest_t;
#endif /* DHD_HMAPTEST */
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
#ifdef AGG_H2D_DB
	osl_atomic_t	inflight;
#endif /* AGG_H2D_DB */
#endif /* TXP_FLUSH_NITEMS */

	uint8   ring_type;
	uint8   n_completion_ids;
	bool    create_pending;
	uint16  create_req_id;
	uint8   current_phase;
	uint16	compeltion_ring_ids[MAX_COMPLETION_RING_IDS_ASSOCIATED];
	uchar		name[RING_NAME_MAX_LENGTH];
	uint32		ring_mem_allocated;
	void	*ring_lock;
} msgbuf_ring_t;

#define DHD_RING_BGN_VA(ring)           ((ring)->dma_buf.va)
#define DHD_RING_END_VA(ring) \
	((uint8 *)(DHD_RING_BGN_VA((ring))) + \
	 (((ring)->max_items - 1) * (ring)->item_len))

#if defined(BCMINTERNAL) && defined(DHD_DBG_DUMP)
#define MAX_IOCTL_TRACE_SIZE    50
#define MAX_IOCTL_BUF_SIZE		64
typedef struct _dhd_ioctl_trace_t {
	uint32	cmd;
	uint16	transid;
	char	ioctl_buf[MAX_IOCTL_BUF_SIZE];
	uint64	timestamp;
} dhd_ioctl_trace_t;
#endif /* defined(BCMINTERNAL) && defined(DHD_DBG_DUMP) */

#ifdef DHD_PKTTS
struct pktts_fwtx_v1 {
	uint32 ts[PKTTS_MAX_FWTX];
};

struct pktts_fwtx_v2 {
	uint32 ts[PKTTS_MAX_FWTX];
	uint32 ut[PKTTS_MAX_UCTX];
	uint32 uc[PKTTS_MAX_UCCNT];
};

static void dhd_msgbuf_send_msg_tx_ts(dhd_pub_t *dhd, void *pkt,
	void *fw_ts, uint16 version);
static void dhd_msgbuf_send_msg_rx_ts(dhd_pub_t *dhd, void *pkt,
	uint fwr1, uint fwr2);
#endif /* DHD_PKTTS */

#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
/** D2H WLAN Rx Packet Chaining context */
typedef struct rxchain_info {
	uint		pkt_count;
	uint		ifidx;
	void		*pkthead;
	void		*pkttail;
	uint8		*h_da;	/* pointer to da of chain head */
	uint8		*h_sa;	/* pointer to sa of chain head */
	uint8		h_prio; /* prio of chain head */
} rxchain_info_t;
#endif /* BCM_ROUTER_DHD && HNDCTF */

/* This can be overwritten by module parameter defined in dhd_linux.c
 * or by dhd iovar h2d_max_txpost.
 */
int h2d_max_txpost = H2DRING_TXPOST_MAX_ITEM;
#if defined(DHD_HTPUT_TUNABLES)
int h2d_htput_max_txpost = H2DRING_HTPUT_TXPOST_MAX_ITEM;
#endif /* DHD_HTPUT_TUNABLES */

#ifdef AGG_H2D_DB
bool agg_h2d_db_enab = TRUE;

#define AGG_H2D_DB_TIMEOUT_USEC		(1000u)	/* 1 msec */
uint32 agg_h2d_db_timeout = AGG_H2D_DB_TIMEOUT_USEC;

#ifndef AGG_H2D_DB_INFLIGHT_THRESH
/* Keep inflight threshold same as txp_threshold */
#define AGG_H2D_DB_INFLIGHT_THRESH TXP_FLUSH_MAX_ITEMS_FLUSH_CNT
#endif /* !AGG_H2D_DB_INFLIGHT_THRESH */

uint32 agg_h2d_db_inflight_thresh = AGG_H2D_DB_INFLIGHT_THRESH;

#define DHD_NUM_INFLIGHT_HISTO_ROWS (14u)
#define DHD_INFLIGHT_HISTO_SIZE (sizeof(uint64) * DHD_NUM_INFLIGHT_HISTO_ROWS)

typedef struct _agg_h2d_db_info {
	void *dhd;
	struct hrtimer timer;
	bool init;
	uint32 direct_db_cnt;
	uint32 timer_db_cnt;
	uint64  *inflight_histo;
} agg_h2d_db_info_t;
#endif /* AGG_H2D_DB */

/** DHD protocol handle. Is an opaque type to other DHD software layers. */
typedef struct dhd_prot {
	osl_t *osh;		/* OSL handle */
	uint16 rxbufpost_sz;
	uint16 rxbufpost;
	uint16 max_rxbufpost;
	uint32 tot_rxbufpost;
	uint32 tot_rxcpl;
	uint16 max_eventbufpost;
	uint16 max_ioctlrespbufpost;
	uint16 max_tsbufpost;
	uint16 max_infobufpost;
	uint16 infobufpost;
	uint16 cur_event_bufs_posted;
	uint16 cur_ioctlresp_bufs_posted;
	uint16 cur_ts_bufs_posted;

	/* Flow control mechanism based on active transmits pending */
	osl_atomic_t active_tx_count; /* increments/decrements on every packet tx/tx_status */
	uint16 h2d_max_txpost;
#if defined(DHD_HTPUT_TUNABLES)
	uint16 h2d_htput_max_txpost;
#endif /* DHD_HTPUT_TUNABLES */
	uint16 txp_threshold;  /* optimization to write "n" tx items at a time to ring */

	/* MsgBuf Ring info: has a dhd_dma_buf that is dynamically allocated */
	msgbuf_ring_t h2dring_ctrl_subn; /* H2D ctrl message submission ring */
	msgbuf_ring_t h2dring_rxp_subn; /* H2D RxBuf post ring */
	msgbuf_ring_t d2hring_ctrl_cpln; /* D2H ctrl completion ring */
	msgbuf_ring_t d2hring_tx_cpln; /* D2H Tx complete message ring */
	msgbuf_ring_t d2hring_rx_cpln; /* D2H Rx complete message ring */
	msgbuf_ring_t *h2dring_info_subn; /* H2D info submission ring */
	msgbuf_ring_t *d2hring_info_cpln; /* D2H info completion ring */
	msgbuf_ring_t *d2hring_edl; /* D2H Enhanced Debug Lane (EDL) ring */

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
#ifdef DHD_DMA_INDICES_SEQNUM
	char *h2d_dma_indx_rd_copy_buf; /* Local copy of H2D WR indices array */
	char *d2h_dma_indx_wr_copy_buf; /* Local copy of D2H WR indices array */
	uint32 h2d_dma_indx_rd_copy_bufsz; /* H2D WR indices array size */
	uint32 d2h_dma_indx_wr_copy_bufsz; /* D2H WR indices array size */
	uint32 host_seqnum;	/* Seqence number for D2H DMA Indices sync */
#endif /* DHD_DMA_INDICES_SEQNUM */
	uint32			flowring_num;

	d2h_sync_cb_t d2h_sync_cb; /* Sync on D2H DMA done: SEQNUM or XORCSUM */
#ifdef EWP_EDL
	d2h_edl_sync_cb_t d2h_edl_sync_cb; /* Sync on EDL D2H DMA done: SEQNUM or XORCSUM */
#endif /* EWP_EDL */
	ulong d2h_sync_wait_max; /* max number of wait loops to receive one msg */
	ulong d2h_sync_wait_tot; /* total wait loops */

	dhd_dmaxfer_t	dmaxfer; /* for test/DMA loopback */

	uint16		ioctl_seq_no;
	uint16		data_seq_no;  /* XXX this field is obsolete */
	uint16		ioctl_trans_id;
	void		*pktid_ctrl_map; /* a pktid maps to a packet and its metadata */
	void		*pktid_rx_map;	/* pktid map for rx path */
	void		*pktid_tx_map;	/* pktid map for tx path */
	bool		metadata_dbg;
	void		*pktid_map_handle_ioctl;
#ifdef DHD_MAP_PKTID_LOGGING
	void		*pktid_dma_map;	/* pktid map for DMA MAP */
	void		*pktid_dma_unmap; /* pktid map for DMA UNMAP */
#endif /* DHD_MAP_PKTID_LOGGING */
	uint32		pktid_depleted_cnt;	/* pktid depleted count */
	/* netif tx queue stop count */
	uint8		pktid_txq_stop_cnt;
	/* netif tx queue start count */
	uint8		pktid_txq_start_cnt;
	uint64		ioctl_fillup_time;	/* timestamp for ioctl fillup */
	uint64		ioctl_ack_time;		/* timestamp for ioctl ack */
	uint64		ioctl_cmplt_time;	/* timestamp for ioctl completion */

	/* Applications/utilities can read tx and rx metadata using IOVARs */
	uint16		rx_metadata_offset;
	uint16		tx_metadata_offset;

#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
	rxchain_info_t	rxchain;	/* chain of rx packets */
#endif

#if defined(DHD_D2H_SOFT_DOORBELL_SUPPORT)
	/* Host's soft doorbell configuration */
	bcmpcie_soft_doorbell_t soft_doorbell[BCMPCIE_D2H_COMMON_MSGRINGS];
#endif /* DHD_D2H_SOFT_DOORBELL_SUPPORT */

	/* Work Queues to be used by the producer and the consumer, and threshold
	 * when the WRITE index must be synced to consumer's workq
	 */
	dhd_dma_buf_t	fw_trap_buf; /* firmware trap buffer */

	uint32  host_ipc_version; /* Host sypported IPC rev */
	uint32  device_ipc_version; /* FW supported IPC rev */
	uint32  active_ipc_version; /* Host advertised IPC rev */
#if defined(BCMINTERNAL) && defined(DHD_DBG_DUMP)
	dhd_ioctl_trace_t	ioctl_trace[MAX_IOCTL_TRACE_SIZE];
	uint32				ioctl_trace_count;
#endif /* defined(BCMINTERNAL) && defined(DHD_DBG_DUMP) */
	dhd_dma_buf_t   hostts_req_buf; /* For holding host timestamp request buf */
	bool    hostts_req_buf_inuse;
	bool    rx_ts_log_enabled;
	bool    tx_ts_log_enabled;
#ifdef BTLOG
	msgbuf_ring_t *h2dring_btlog_subn; /* H2D btlog submission ring */
	msgbuf_ring_t *d2hring_btlog_cpln; /* D2H btlog completion ring */
	uint16 btlogbufpost;
	uint16 max_btlogbufpost;
#endif	/* BTLOG */
#ifdef DHD_HMAPTEST
	uint32 hmaptest_rx_active;
	uint32 hmaptest_rx_pktid;
	char *hmap_rx_buf_va;
	dmaaddr_t hmap_rx_buf_pa;
	uint32 hmap_rx_buf_len;

	uint32 hmaptest_tx_active;
	uint32 hmaptest_tx_pktid;
	char *hmap_tx_buf_va;
	dmaaddr_t hmap_tx_buf_pa;
	uint32	  hmap_tx_buf_len;
	dhd_hmaptest_t	hmaptest; /* for hmaptest */
	bool hmap_enabled; /* TRUE = hmap is enabled */
#endif /* DHD_HMAPTEST */
#ifdef SNAPSHOT_UPLOAD
	dhd_dma_buf_t snapshot_upload_buf;	/* snapshot upload buffer */
	uint32 snapshot_upload_len;		/* snapshot uploaded len */
	uint8 snapshot_type;			/* snaphot uploaded type */
	bool snapshot_cmpl_pending;		/* snapshot completion pending */
#endif	/* SNAPSHOT_UPLOAD */
	bool no_retry;
	bool no_aggr;
	bool fixed_rate;
	dhd_dma_buf_t	host_scb_buf; /* scb host offload buffer */
#ifdef DHD_HP2P
	msgbuf_ring_t *d2hring_hp2p_txcpl; /* D2H HPP Tx completion ring */
	msgbuf_ring_t *d2hring_hp2p_rxcpl; /* D2H HPP Rx completion ring */
#endif /* DHD_HP2P */
	bool no_tx_resource;
	uint32 txcpl_db_cnt;
#ifdef AGG_H2D_DB
	agg_h2d_db_info_t agg_h2d_db_info;
#endif /* AGG_H2D_DB */
	uint64 tx_h2d_db_cnt;
} dhd_prot_t;

#ifdef DHD_EWPR_VER2
#define HANG_INFO_BASE64_BUFFER_SIZE 640
#endif

#ifdef DHD_DUMP_PCIE_RINGS
static
int dhd_ring_write(dhd_pub_t *dhd, msgbuf_ring_t *ring, void *file,
	const void *user_buf, unsigned long *file_posn);
#ifdef EWP_EDL
static
int dhd_edl_ring_hdr_write(dhd_pub_t *dhd, msgbuf_ring_t *ring, void *file, const void *user_buf,
	unsigned long *file_posn);
#endif /* EWP_EDL */
#endif /* DHD_DUMP_PCIE_RINGS */
extern bool dhd_timesync_delay_post_bufs(dhd_pub_t *dhdp);
extern void dhd_schedule_dmaxfer_free(dhd_pub_t* dhdp, dmaxref_mem_map_t *dmmap);
/* Convert a dmaaddr_t to a base_addr with htol operations */
static INLINE void dhd_base_addr_htolpa(sh_addr_t *base_addr, dmaaddr_t pa);

/* APIs for managing a DMA-able buffer */
static int  dhd_dma_buf_audit(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf);
static void dhd_dma_buf_reset(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf);

/* msgbuf ring management */
static int dhd_prot_allocate_bufs(dhd_pub_t *dhd, dhd_prot_t *prot);
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

#ifdef AGG_H2D_DB
static void dhd_prot_agg_db_ring_write(dhd_pub_t *dhd, msgbuf_ring_t * ring,
		void* p, uint16 len);
static void dhd_prot_aggregate_db_ring_door_bell(dhd_pub_t *dhd, uint16 flowid, bool ring_db);
static void dhd_prot_txdata_aggr_db_write_flush(dhd_pub_t *dhd, uint16 flowid);
#endif /* AGG_H2D_DB */
static void dhd_prot_ring_doorbell(dhd_pub_t *dhd, uint32 value);
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

static void dhd_prot_return_rxbuf(dhd_pub_t *dhd, msgbuf_ring_t *ring, uint32 pktid, uint32 rxcnt);

#if defined(BCMINTERNAL) && defined(DHD_DBG_DUMP)
static void dhd_prot_ioctl_trace(dhd_pub_t *dhd, ioctl_req_msg_t *ioct_rqst, uchar *buf, int len);
static void dhd_prot_ioctl_dump(dhd_prot_t *prot, struct bcmstrbuf *strbuf);
#endif /* defined(BCMINTERNAL) && defined(DHD_DBG_DUMP) */

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
#if !defined(BCM_ROUTER_DHD)
static void dhd_prot_process_h2d_ring_create_complete(dhd_pub_t *dhd, void *buf);
static void dhd_prot_process_infobuf_complete(dhd_pub_t *dhd, void* buf);
#endif /* !BCM_ROUTER_DHD */
static void dhd_prot_process_d2h_mb_data(dhd_pub_t *dhd, void* buf);
static void dhd_prot_detach_info_rings(dhd_pub_t *dhd);
#ifdef BTLOG
static void dhd_prot_process_btlog_complete(dhd_pub_t *dhd, void* buf);
static void dhd_prot_detach_btlog_rings(dhd_pub_t *dhd);
#endif	/* BTLOG */
#ifdef DHD_HP2P
static void dhd_prot_detach_hp2p_rings(dhd_pub_t *dhd);
#endif /* DHD_HP2P */
#ifdef EWP_EDL
static void dhd_prot_detach_edl_rings(dhd_pub_t *dhd);
#endif
static void dhd_prot_process_d2h_host_ts_complete(dhd_pub_t *dhd, void* buf);
static void dhd_prot_process_snapshot_complete(dhd_pub_t *dhd, void *buf);

#ifdef DHD_TIMESYNC
extern void dhd_parse_proto(uint8 *pktdata, dhd_pkt_parse_t *parse);
#endif

#ifdef DHD_FLOW_RING_STATUS_TRACE
void dhd_dump_bus_flow_ring_status_isr_trace(dhd_bus_t *bus, struct bcmstrbuf *strbuf);
void dhd_dump_bus_flow_ring_status_dpc_trace(dhd_bus_t *bus, struct bcmstrbuf *strbuf);
#endif /* DHD_FLOW_RING_STATUS_TRACE */

#ifdef DHD_TX_PROFILE
extern bool dhd_protocol_matches_profile(uint8 *p, int plen, const
		dhd_tx_profile_protocol_t *proto, bool is_host_sfhllc);
#endif /* defined(DHD_TX_PROFILE) */

#ifdef DHD_HP2P
static void dhd_update_hp2p_rxstats(dhd_pub_t *dhd, host_rxbuf_cmpl_t *rxstatus);
static void dhd_update_hp2p_txstats(dhd_pub_t *dhd, host_txbuf_cmpl_t *txstatus);
static void dhd_calc_hp2p_burst(dhd_pub_t *dhd, msgbuf_ring_t *ring, uint16 flowid);
static void dhd_update_hp2p_txdesc(dhd_pub_t *dhd, host_txbuf_post_t *txdesc);
#endif
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
#if defined(BCM_ROUTER_DHD)
	NULL, /* MSG_TYPE_INFO_BUF_CMPLT */
#else
	dhd_prot_process_infobuf_complete, /* MSG_TYPE_INFO_BUF_CMPLT */
#endif /* BCM_ROUTER_DHD */
	NULL, /* MSG_TYPE_H2D_RING_CREATE */
	NULL, /* MSG_TYPE_D2H_RING_CREATE */
#if defined(BCM_ROUTER_DHD)
	NULL, /* MSG_TYPE_H2D_RING_CREATE_CMPLT */
#else
	dhd_prot_process_h2d_ring_create_complete, /* MSG_TYPE_H2D_RING_CREATE_CMPLT */
#endif /* BCM_ROUTER_DHD */
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
	NULL,	/* MSG_TYPE_SNAPSHOT_UPLOAD */
	dhd_prot_process_snapshot_complete,	/* MSG_TYPE_SNAPSHOT_CMPLT */
};

#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
/* Related to router CPU mapping per radio core */
#define DHD_RX_CHAINING
#endif /* BCM_ROUTER_DHD && HNDCTF */

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

static INLINE void dhd_rxchain_reset(rxchain_info_t *rxchain);
static void dhd_rxchain_frame(dhd_pub_t *dhd, void *pkt, uint ifidx);
static void dhd_rxchain_commit(dhd_pub_t *dhd);

#define DHD_PKT_CTF_MAX_CHAIN_LEN	64

#endif /* DHD_RX_CHAINING */

#ifdef DHD_EFI
#define DHD_LPBKDTDUMP_ON()	(1)
#else
#define DHD_LPBKDTDUMP_ON()	(dhd_msg_level & DHD_LPBKDTDUMP_VAL)
#endif

static void dhd_prot_h2d_sync_init(dhd_pub_t *dhd);

#ifdef D2H_MINIDUMP
dhd_dma_buf_t *
dhd_prot_get_minidump_buf(dhd_pub_t *dhd)
{
	return &dhd->prot->fw_trap_buf;
}
#endif /* D2H_MINIDUMP */

uint16
dhd_prot_get_rxbufpost_sz(dhd_pub_t *dhd)
{
	return dhd->prot->rxbufpost_sz;
}

uint16
dhd_prot_get_h2d_rx_post_active(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *flow_ring = &prot->h2dring_rxp_subn;
	uint16 rd, wr;

	/* Since wr is owned by host in h2d direction, directly read wr */
	wr = flow_ring->wr;

	if (dhd->dma_d2h_ring_upd_support) {
		rd = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, flow_ring->idx);
	} else {
		dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, flow_ring->idx);
	}
	return NTXPACTIVE(rd, wr, flow_ring->max_items);
}

uint16
dhd_prot_get_d2h_rx_cpln_active(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *flow_ring = &prot->d2hring_rx_cpln;
	uint16 rd, wr;

	if (dhd->dma_d2h_ring_upd_support) {
		wr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, flow_ring->idx);
	} else {
		dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, flow_ring->idx);
	}

	/* Since rd is owned by host in d2h direction, directly read rd */
	rd = flow_ring->rd;

	return NTXPACTIVE(rd, wr, flow_ring->max_items);
}

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

void
dhd_prot_dump_ring_ptrs(void *prot_info)
{
	msgbuf_ring_t *ring = (msgbuf_ring_t *)prot_info;
	DHD_ERROR(("%s curr_rd: %d rd: %d wr: %d \n", __FUNCTION__,
		ring->curr_rd, ring->rd, ring->wr));
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
#if defined(DHD_HTPUT_TUNABLES)
uint16
dhd_prot_get_h2d_htput_max_txpost(dhd_pub_t *dhd)
{
	return (uint16)h2d_htput_max_txpost;
}
void
dhd_prot_set_h2d_htput_max_txpost(dhd_pub_t *dhd, uint16 htput_max_txpost)
{
	h2d_htput_max_txpost = htput_max_txpost;
}

#endif /* DHD_HTPUT_TUNABLES */
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
static int dhd_send_d2h_ringcreate(dhd_pub_t *dhd, msgbuf_ring_t *ring_to_create,
	uint16 ring_type, uint32 id);
static int dhd_send_h2d_ringcreate(dhd_pub_t *dhd, msgbuf_ring_t *ring_to_create,
	uint8 type, uint32 id);

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

	if (dhd_query_bus_erros(dhd)) {
		return;
	}

	DHD_ERROR((
		"LIVELOCK DHD<%p> ring<%s> msg_seqnum<%u> ring_seqnum<%u:%u> tries<%u> max<%lu>"
		" tot<%lu> dma_buf va<%p> msg<%p> curr_rd<%d> rd<%d> wr<%d>\n",
		dhd, ring->name, msg_seqnum, ring_seqnum, ring_seqnum% D2H_EPOCH_MODULO, tries,
		dhd->prot->d2h_sync_wait_max, dhd->prot->d2h_sync_wait_tot,
		ring->dma_buf.va, msg, ring->curr_rd, ring->rd, ring->wr));

	dhd_prhex("D2H MsgBuf Failure", msg, msglen, DHD_ERROR_VAL);

	/* Try to resume if already suspended or suspend in progress */
#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(dhd, CAN_SLEEP(), __builtin_return_address(0));
#endif /* DHD_PCIE_RUNTIMEPM */

	/* Skip if still in suspended or suspend in progress */
	if (DHD_BUS_CHECK_SUSPEND_OR_ANY_SUSPEND_IN_PROGRESS(dhd)) {
		DHD_ERROR(("%s: bus is in suspend(%d) or suspending(0x%x) state, so skip\n",
			__FUNCTION__, dhd->busstate, dhd->dhd_bus_busy_state));
		goto exit;
	}

	dhd_bus_dump_console_buffer(dhd->bus);
	dhd_prot_debug_info_print(dhd);

#ifdef DHD_FW_COREDUMP
	if (dhd->memdump_enabled) {
		/* collect core dump */
		dhd->memdump_type = DUMP_TYPE_BY_LIVELOCK;
		dhd_bus_mem_dump(dhd);
	}
#endif /* DHD_FW_COREDUMP */

exit:
	dhd_schedule_reset(dhd);

#ifdef OEM_ANDROID
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
	dhd->bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
	/* XXX Trigger HANG event for recovery */
	dhd->hang_reason = HANG_REASON_MSGBUF_LIVELOCK;
	dhd_os_send_hang_message(dhd);
#endif /* SUPPORT_LINKDOWN_RECOVERY */
#endif /* OEM_ANDROID */
	dhd->livelock_occured = TRUE;
}

/**
 * dhd_prot_d2h_sync_seqnum - Sync on a D2H DMA completion using the SEQNUM
 * mode. Sequence number is always in the last word of a message.
 */
static uint8
BCMFASTPATH(dhd_prot_d2h_sync_seqnum)(dhd_pub_t *dhd, msgbuf_ring_t *ring,
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
				/* Check for LIVELOCK induce flag, which is set by firing
				 * dhd iovar to induce LIVELOCK error. If flag is set,
				 * MSG_TYPE_INVALID is returned, which results in to LIVELOCK error.
				 */
				if (dhd->dhd_induce_error != DHD_INDUCE_LIVELOCK) {
					goto dma_completed;
				}
			}

			total_tries = (uint32)(((step-1) * PCIE_D2H_SYNC_WAIT_TRIES) + tries);

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
static uint8
BCMFASTPATH(dhd_prot_d2h_sync_xorcsum)(dhd_pub_t *dhd, msgbuf_ring_t *ring,
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
			/* First verify if the seqnumber has been update,
			 * if yes, then only check xorcsum.
			 * Once seqnum and xorcsum is proper that means
			 * complete message has arrived.
			 */
			if (msg->epoch == ring_seqnum) {
				prot_checksum = bcm_compute_xor32((volatile uint32 *)msg,
					num_words);
				if (prot_checksum == 0U) { /* checksum is OK */
					ring->seqnum++; /* next expected sequence number */
					/* Check for LIVELOCK induce flag, which is set by firing
					 * dhd iovar to induce LIVELOCK error. If flag is set,
					 * MSG_TYPE_INVALID is returned, which results in to
					 * LIVELOCK error.
					 */
					if (dhd->dhd_induce_error != DHD_INDUCE_LIVELOCK) {
						goto dma_completed;
					}
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
static uint8
BCMFASTPATH(dhd_prot_d2h_sync_none)(dhd_pub_t *dhd, msgbuf_ring_t *ring,
                       volatile cmn_msg_hdr_t *msg, int msglen)
{
	/* Check for LIVELOCK induce flag, which is set by firing
	* dhd iovar to induce LIVELOCK error. If flag is set,
	* MSG_TYPE_INVALID is returned, which results in to LIVELOCK error.
	*/
	if (dhd->dhd_induce_error == DHD_INDUCE_LIVELOCK) {
		DHD_ERROR(("%s: Inducing livelock\n", __FUNCTION__));
		return MSG_TYPE_INVALID;
	} else {
		return msg->msg_type;
	}
}

#ifdef EWP_EDL
/**
 * dhd_prot_d2h_sync_edl - Sync on a D2H DMA completion by validating the cmn_msg_hdr_t
 * header values at both the beginning and end of the payload.
 * The cmn_msg_hdr_t is placed at the start and end of the payload
 * in each work item in the EDL ring.
 * Dongle will place a seqnum inside the cmn_msg_hdr_t 'epoch' field
 * and the length of the payload in the 'request_id' field.
 * Structure of each work item in the EDL ring:
 * | cmn_msg_hdr_t | payload (var len) | cmn_msg_hdr_t |
 * NOTE: - it was felt that calculating xorcsum for the entire payload (max length of 1648 bytes) is
 * too costly on the dongle side and might take up too many ARM cycles,
 * hence the xorcsum sync method is not being used for EDL ring.
 */
static int
BCMFASTPATH(dhd_prot_d2h_sync_edl)(dhd_pub_t *dhd, msgbuf_ring_t *ring,
                          volatile cmn_msg_hdr_t *msg)
{
	uint32 tries;
	int msglen = 0, len = 0;
	uint32 ring_seqnum = ring->seqnum % D2H_EPOCH_MODULO;
	dhd_prot_t *prot = dhd->prot;
	uint32 step = 0;
	uint32 delay = PCIE_D2H_SYNC_DELAY;
	uint32 total_tries = 0;
	volatile cmn_msg_hdr_t *trailer = NULL;
	volatile uint8 *buf = NULL;
	bool valid_msg = FALSE;

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
			/* First verify if the seqnumber has been updated,
			 * if yes, only then validate the header and trailer.
			 * Once seqnum, header and trailer have been validated, it means
			 * that the complete message has arrived.
			 */
			valid_msg = FALSE;
			if (msg->epoch == ring_seqnum &&
				msg->msg_type == MSG_TYPE_INFO_PYLD &&
				msg->request_id > 0 &&
				msg->request_id <= ring->item_len) {
				/* proceed to check trailer only if header is valid */
				buf = (volatile uint8 *)msg;
				msglen = sizeof(cmn_msg_hdr_t) + msg->request_id;
				buf += msglen;
				if (msglen + sizeof(cmn_msg_hdr_t) <= ring->item_len) {
					trailer = (volatile cmn_msg_hdr_t *)buf;
					valid_msg = (trailer->epoch == ring_seqnum) &&
						(trailer->msg_type == msg->msg_type) &&
						(trailer->request_id == msg->request_id);
					if (!valid_msg) {
						DHD_TRACE(("%s:invalid trailer! seqnum=%u;reqid=%u"
						" expected, seqnum=%u; reqid=%u. Retrying... \n",
						__FUNCTION__, trailer->epoch, trailer->request_id,
						msg->epoch, msg->request_id));
					}
				} else {
					DHD_TRACE(("%s: invalid payload length (%u)! Retrying.. \n",
						__FUNCTION__, msg->request_id));
				}

				if (valid_msg) {
					/* data is OK */
					ring->seqnum++; /* next expected sequence number */
					if (dhd->dhd_induce_error != DHD_INDUCE_LIVELOCK) {
						goto dma_completed;
					}
				}
			} else {
				DHD_TRACE(("%s: wrong hdr, seqnum expected %u, got %u."
					" msg_type=0x%x, request_id=%u."
					" Retrying...\n",
					__FUNCTION__, ring_seqnum, msg->epoch,
					msg->msg_type, msg->request_id));
			}

			total_tries = ((step-1) * PCIE_D2H_SYNC_WAIT_TRIES) + tries;

			if (total_tries > prot->d2h_sync_wait_max)
				prot->d2h_sync_wait_max = total_tries;

			OSL_CACHE_INV(msg, msglen); /* invalidate and try again */
#if !(defined(BCM_ROUTER_DHD) && defined(BCM_GMAC3))
			OSL_CPU_RELAX(); /* CPU relax for msg_seqnum  value to update */
			OSL_DELAY(delay * step); /* Add stepper delay */
#endif /* !(defined(BCM_ROUTER_DHD) && defined(BCM_GMAC3)) */

		} /* for PCIE_D2H_SYNC_WAIT_TRIES */
	} /* for PCIE_D2H_SYNC_NUM_OF_STEPS */

	DHD_ERROR(("%s: EDL header check fails !\n", __FUNCTION__));
	DHD_ERROR(("%s: header: seqnum=%u; expected-seqnum=%u"
		" msgtype=0x%x; expected-msgtype=0x%x"
		" length=%u; expected-max-length=%u", __FUNCTION__,
		msg->epoch, ring_seqnum, msg->msg_type, MSG_TYPE_INFO_PYLD,
		msg->request_id, ring->item_len));
	dhd_prhex("msg header bytes: ", (volatile uchar *)msg, sizeof(*msg), DHD_ERROR_VAL);
	if (trailer && msglen > 0 &&
			(msglen + sizeof(cmn_msg_hdr_t)) <= ring->item_len) {
		DHD_ERROR(("%s: trailer: seqnum=%u; expected-seqnum=%u"
			" msgtype=0x%x; expected-msgtype=0x%x"
			" length=%u; expected-length=%u", __FUNCTION__,
			trailer->epoch, ring_seqnum, trailer->msg_type, MSG_TYPE_INFO_PYLD,
			trailer->request_id, msg->request_id));
		dhd_prhex("msg trailer bytes: ", (volatile uchar *)trailer,
			sizeof(*trailer), DHD_ERROR_VAL);
	}

	if ((msglen + sizeof(cmn_msg_hdr_t)) <= ring->item_len)
		len = msglen + sizeof(cmn_msg_hdr_t);
	else
		len = ring->item_len;

	dhd_prot_d2h_sync_livelock(dhd, msg->epoch, ring, total_tries,
		(volatile uchar *) msg, len);

	ring->seqnum++; /* skip this message */
	return BCME_ERROR; /* invalid msg_type 0 -> noop callback */

dma_completed:
	DHD_TRACE(("%s: EDL header check pass, seqnum=%u; reqid=%u\n", __FUNCTION__,
		msg->epoch, msg->request_id));

	prot->d2h_sync_wait_tot += tries;
	return BCME_OK;
}

/**
 * dhd_prot_d2h_sync_edl_none - Dongle ensure that the DMA will complete and host
 * need to try to sync. This noop sync handler will be bound when the dongle
 * advertises that neither the SEQNUM nor XORCSUM mode of DMA sync is required.
 */
static int BCMFASTPATH
(dhd_prot_d2h_sync_edl_none)(dhd_pub_t *dhd, msgbuf_ring_t *ring,
                       volatile cmn_msg_hdr_t *msg)
{
	/* Check for LIVELOCK induce flag, which is set by firing
	* dhd iovar to induce LIVELOCK error. If flag is set,
	* MSG_TYPE_INVALID is returned, which results in to LIVELOCK error.
	*/
	if (dhd->dhd_induce_error == DHD_INDUCE_LIVELOCK) {
		DHD_ERROR(("%s: Inducing livelock\n", __FUNCTION__));
		return BCME_ERROR;
	} else {
		if (msg->msg_type == MSG_TYPE_INFO_PYLD)
			return BCME_OK;
		else
			return msg->msg_type;
	}
}
#endif /* EWP_EDL */

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
#ifdef EWP_EDL
		prot->d2h_edl_sync_cb = dhd_prot_d2h_sync_edl;
#endif /* EWP_EDL */
		DHD_ERROR(("%s(): D2H sync mechanism is SEQNUM \r\n", __FUNCTION__));
	} else if (dhd->d2h_sync_mode & PCIE_SHARED_D2H_SYNC_XORCSUM) {
		prot->d2h_sync_cb = dhd_prot_d2h_sync_xorcsum;
#ifdef EWP_EDL
		prot->d2h_edl_sync_cb = dhd_prot_d2h_sync_edl;
#endif /* EWP_EDL */
		DHD_ERROR(("%s(): D2H sync mechanism is XORCSUM \r\n", __FUNCTION__));
	} else {
		prot->d2h_sync_cb = dhd_prot_d2h_sync_none;
#ifdef EWP_EDL
		prot->d2h_edl_sync_cb = dhd_prot_d2h_sync_edl_none;
#endif /* EWP_EDL */
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
int
dhd_dma_buf_alloc(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf, uint32 buf_len)
{
	uint32 dma_pad = 0;
	osl_t *osh = dhd->osh;
	uint16 dma_align = DMA_ALIGN_LEN;
	uint32 rem = 0;

	ASSERT(dma_buf != NULL);
	ASSERT(dma_buf->va == NULL);
	ASSERT(dma_buf->len == 0);

	/* Pad the buffer length to align to cacheline size. */
	rem = (buf_len % DHD_DMA_PAD);
	dma_pad = rem ? (DHD_DMA_PAD - rem) : 0;

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

void
dhd_local_buf_reset(char *buf, uint32 len)
{
	/* Zero out the entire buffer and cache flush */
	memset((void*)buf, 0, len);
	OSL_CACHE_FLUSH((void *)buf, len);
}

/**
 * dhd_dma_buf_free - Free a DMA-able buffer that was previously allocated using
 * dhd_dma_buf_alloc().
 */
void
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
 * DHD_MAP_PKTID_LOGGING
 * Logging the PKTID and DMA map/unmap information for the SMMU fault issue
 * debugging in customer platform.
 * +---------------------------------------------------------------------------+
 */

#ifdef DHD_MAP_PKTID_LOGGING
typedef struct dhd_pktid_log_item {
	dmaaddr_t pa;		/* DMA bus address */
	uint64 ts_nsec;		/* Timestamp: nsec */
	uint32 size;		/* DMA map/unmap size */
	uint32 pktid;		/* Packet ID */
	uint8 pkttype;		/* Packet Type */
	uint8 rsvd[7];		/* Reserved for future use */
} dhd_pktid_log_item_t;

typedef struct dhd_pktid_log {
	uint32 items;		/* number of total items */
	uint32 index;		/* index of pktid_log_item */
	dhd_pktid_log_item_t map[0];	/* metadata storage */
} dhd_pktid_log_t;

typedef void * dhd_pktid_log_handle_t; /* opaque handle to pktid log */

#define	MAX_PKTID_LOG				(2048)
#define DHD_PKTID_LOG_ITEM_SZ			(sizeof(dhd_pktid_log_item_t))
#define DHD_PKTID_LOG_SZ(items)			(uint32)((sizeof(dhd_pktid_log_t)) + \
					((DHD_PKTID_LOG_ITEM_SZ) * (items)))

#define DHD_PKTID_LOG_INIT(dhd, hdl)		dhd_pktid_logging_init((dhd), (hdl))
#define DHD_PKTID_LOG_FINI(dhd, hdl)		dhd_pktid_logging_fini((dhd), (hdl))
#define DHD_PKTID_LOG(dhd, hdl, pa, pktid, len, pkttype)	\
	dhd_pktid_logging((dhd), (hdl), (pa), (pktid), (len), (pkttype))
#define DHD_PKTID_LOG_DUMP(dhd)			dhd_pktid_logging_dump((dhd))

static dhd_pktid_log_handle_t *
dhd_pktid_logging_init(dhd_pub_t *dhd, uint32 num_items)
{
	dhd_pktid_log_t *log;
	uint32 log_size;

	log_size = DHD_PKTID_LOG_SZ(num_items);
	log = (dhd_pktid_log_t *)MALLOCZ(dhd->osh, log_size);
	if (log == NULL) {
		DHD_ERROR(("%s: MALLOC failed for size %d\n",
			__FUNCTION__, log_size));
		return (dhd_pktid_log_handle_t *)NULL;
	}

	log->items = num_items;
	log->index = 0;

	return (dhd_pktid_log_handle_t *)log; /* opaque handle */
}

static void
dhd_pktid_logging_fini(dhd_pub_t *dhd, dhd_pktid_log_handle_t *handle)
{
	dhd_pktid_log_t *log;
	uint32 log_size;

	if (handle == NULL) {
		DHD_ERROR(("%s: handle is NULL\n", __FUNCTION__));
		return;
	}

	log = (dhd_pktid_log_t *)handle;
	log_size = DHD_PKTID_LOG_SZ(log->items);
	MFREE(dhd->osh, handle, log_size);
}

static void
dhd_pktid_logging(dhd_pub_t *dhd, dhd_pktid_log_handle_t *handle, dmaaddr_t pa,
	uint32 pktid, uint32 len, uint8 pkttype)
{
	dhd_pktid_log_t *log;
	uint32 idx;

	if (handle == NULL) {
		DHD_ERROR(("%s: handle is NULL\n", __FUNCTION__));
		return;
	}

	log = (dhd_pktid_log_t *)handle;
	idx = log->index;
	log->map[idx].ts_nsec = OSL_LOCALTIME_NS();
	log->map[idx].pa = pa;
	log->map[idx].pktid = pktid;
	log->map[idx].size = len;
	log->map[idx].pkttype = pkttype;
	log->index = (idx + 1) % (log->items);	/* update index */
}

void
dhd_pktid_logging_dump(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	dhd_pktid_log_t *map_log, *unmap_log;
	uint64 ts_sec, ts_usec;

	if (prot == NULL) {
		DHD_ERROR(("%s: prot is NULL\n", __FUNCTION__));
		return;
	}

	map_log = (dhd_pktid_log_t *)(prot->pktid_dma_map);
	unmap_log = (dhd_pktid_log_t *)(prot->pktid_dma_unmap);
	OSL_GET_LOCALTIME(&ts_sec, &ts_usec);
	if (map_log && unmap_log) {
		DHD_ERROR(("%s: map_idx=%d unmap_idx=%d "
			"current time=[%5lu.%06lu]\n", __FUNCTION__,
			map_log->index, unmap_log->index,
			(unsigned long)ts_sec, (unsigned long)ts_usec));
		DHD_ERROR(("%s: pktid_map_log(pa)=0x%llx size=%d, "
			"pktid_unmap_log(pa)=0x%llx size=%d\n", __FUNCTION__,
			(uint64)__virt_to_phys((ulong)(map_log->map)),
			(uint32)(DHD_PKTID_LOG_ITEM_SZ * map_log->items),
			(uint64)__virt_to_phys((ulong)(unmap_log->map)),
			(uint32)(DHD_PKTID_LOG_ITEM_SZ * unmap_log->items)));
	}
}
#endif /* DHD_MAP_PKTID_LOGGING */

/* +-----------------  End of DHD_MAP_PKTID_LOGGING -----------------------+ */

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

/* On Router, the pktptr serves as a pktid. */
#if defined(BCM_ROUTER_DHD) && !defined(BCA_HNDROUTER)
#undef DHD_PCIE_PKTID		/* Comment this undef, to reenable PKTIDMAP */
#endif /* BCM_ROUTER_DHD && !BCA_HNDROUTER */

#if defined(BCM_ROUTER_DHD) && defined(DHD_PCIE_PKTID)
#undef MAX_TX_PKTID
#define MAX_TX_PKTID     ((36 * 1024) - 1) /* Extend for 64 clients support. */
#endif /* BCM_ROUTER_DHD && DHD_PCIE_PKTID */

/* XXX: PROP_TXSTATUS: WLFS defines a private pkttag layout.
 * Hence cannot store the dma parameters in the pkttag and the pktidmap locker
 * is required.
 */
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

#define DHD_PKTID_MIN_AVAIL_COUNT		512U
#define DHD_PKTID_DEPLETED_MAX_COUNT		(DHD_PKTID_MIN_AVAIL_COUNT * 2U)
#define DHD_PKTID_INVALID			(0U)
#define DHD_IOCTL_REQ_PKTID			(0xFFFE)
#define DHD_FAKE_PKTID				(0xFACE)
#define DHD_H2D_DBGRING_REQ_PKTID		0xFFFD
#define DHD_D2H_DBGRING_REQ_PKTID		0xFFFC
#define DHD_H2D_HOSTTS_REQ_PKTID		0xFFFB
#define DHD_H2D_BTLOGRING_REQ_PKTID		0xFFFA
#define DHD_D2H_BTLOGRING_REQ_PKTID		0xFFF9
#define DHD_H2D_SNAPSHOT_UPLOAD_REQ_PKTID	0xFFF8
#ifdef DHD_HP2P
#define DHD_D2H_HPPRING_TXREQ_PKTID		0xFFF7
#define DHD_D2H_HPPRING_RXREQ_PKTID		0xFFF6
#endif /* DHD_HP2P */

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

#if defined(DHD_PCIE_PKTID)
#if defined(NDIS) || defined(DHD_EFI)
/* XXX: for NDIS, using consistent memory instead of buffer from PKTGET for
 * up to 8K ioctl response
 */
#define IOCTLRESP_USE_CONSTMEM
static void free_ioctl_return_buffer(dhd_pub_t *dhd, dhd_dma_buf_t *retbuf);
static int  alloc_ioctl_return_buffer(dhd_pub_t *dhd, dhd_dma_buf_t *retbuf);
#endif /* NDIS || DHD_EFI */

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

#ifdef DHD_PKTTS
/* Store the Metadata buffer to the locker */
static INLINE void
dhd_pktid_map_save_metadata(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle, void *mpkt,
	dmaaddr_t mpkt_pa,
	uint16	mpkt_len,
	void *dmah,
	uint32 nkey);

/* Return the Metadata buffer from the locker */
static void * dhd_pktid_map_retreive_metadata(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle,
	dmaaddr_t *pmpkt_pa, uint32 *pmpkt_len, void **pdmah, uint32 nkey);
#endif /* DHD_PKTTS */

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

typedef enum dhd_pktid_map_type {
	DHD_PKTID_MAP_TYPE_CTRL = 1,
	DHD_PKTID_MAP_TYPE_TX,
	DHD_PKTID_MAP_TYPE_RX,
	DHD_PKTID_MAP_TYPE_UNKNOWN
} dhd_pktid_map_type_t;

#ifdef USE_DHD_PKTID_AUDIT_LOCK
#define DHD_PKTID_AUDIT_LOCK_INIT(osh)          osl_spin_lock_init(osh)
#define DHD_PKTID_AUDIT_LOCK_DEINIT(osh, lock)  osl_spin_lock_deinit(osh, lock)
#define DHD_PKTID_AUDIT_LOCK(lock)              osl_spin_lock(lock)
#define DHD_PKTID_AUDIT_UNLOCK(lock, flags)     osl_spin_unlock(lock, flags)
#else
#define DHD_PKTID_AUDIT_LOCK_INIT(osh)          (void *)(1)
#define DHD_PKTID_AUDIT_LOCK_DEINIT(osh, lock)  do { /* noop */ } while (0)
#define DHD_PKTID_AUDIT_LOCK(lock)              0
#define DHD_PKTID_AUDIT_UNLOCK(lock, flags)     do { /* noop */ } while (0)
#endif /* !USE_DHD_PKTID_AUDIT_LOCK */

#endif /* DHD_PKTID_AUDIT_ENABLED */

#define USE_DHD_PKTID_LOCK   1

#ifdef USE_DHD_PKTID_LOCK
#define DHD_PKTID_LOCK_INIT(osh)                osl_spin_lock_init(osh)
#define DHD_PKTID_LOCK_DEINIT(osh, lock)        osl_spin_lock_deinit(osh, lock)
#define DHD_PKTID_LOCK(lock, flags)             (flags) = osl_spin_lock(lock)
#define DHD_PKTID_UNLOCK(lock, flags)           osl_spin_unlock(lock, flags)
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
#ifdef DHD_PKTTS
	void		*mpkt;    /* VA of Metadata */
	dmaaddr_t	mpkt_pa;  /* PA of Metadata */
	uint16		mpkt_len; /* Length of Metadata */
#endif /* DHD_PKTTS */
} dhd_pktid_item_t;

typedef uint32 dhd_pktid_key_t;

typedef struct dhd_pktid_map {
	uint32      items;    /* total items in map */
	uint32      avail;    /* total available items */
	int         failures; /* lockers unavailable count */
	/* Spinlock to protect dhd_pktid_map in process/tasklet context */
	void        *pktid_lock; /* Used when USE_DHD_PKTID_LOCK is defined */

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

#ifdef DHD_PKTTS
#define DHD_PKTID_SAVE_METADATA(dhd, map, mpkt, mpkt_pa, mpkt_len, dmah, nkey) \
	dhd_pktid_map_save_metadata(dhd, map, mpkt, mpkt_pa, mpkt_len, dmah, nkey)

#define DHD_PKTID_RETREIVE_METADATA(dhd, map, mpkt_pa, mpkt_len, dmah, nkey) \
	dhd_pktid_map_retreive_metadata(dhd, map, (dmaaddr_t *)&mpkt_pa, (uint32 *)&mpkt_len, \
		(void **) &dmah, nkey)
#endif /* DHD_PKTTS */

#define DHD_PKTID_AVAIL(map)                 dhd_pktid_map_avail_cnt(map)

#if defined(DHD_PKTID_AUDIT_ENABLED)

static int
dhd_get_pktid_map_type(dhd_pub_t *dhd, dhd_pktid_map_t *pktid_map)
{
	dhd_prot_t *prot = dhd->prot;
	int pktid_map_type;

	if (pktid_map == prot->pktid_ctrl_map) {
		pktid_map_type = DHD_PKTID_MAP_TYPE_CTRL;
	} else if (pktid_map == prot->pktid_tx_map) {
		pktid_map_type = DHD_PKTID_MAP_TYPE_TX;
	} else if (pktid_map == prot->pktid_rx_map) {
		pktid_map_type = DHD_PKTID_MAP_TYPE_RX;
	} else {
		pktid_map_type = DHD_PKTID_MAP_TYPE_UNKNOWN;
	}

	return pktid_map_type;
}

/**
* __dhd_pktid_audit - Use the mwbmap to audit validity of a pktid.
*/
static int
__dhd_pktid_audit(dhd_pub_t *dhd, dhd_pktid_map_t *pktid_map, uint32 pktid,
	const int test_for, const char *errmsg)
{
#define DHD_PKT_AUDIT_STR "ERROR: %16s Host PktId Audit: "
	struct bcm_mwbmap *handle;
	uint32	flags;
	bool ignore_audit;
	int error = BCME_OK;

	if (pktid_map == (dhd_pktid_map_t *)NULL) {
		DHD_ERROR((DHD_PKT_AUDIT_STR "Pkt id map NULL\n", errmsg));
		return BCME_OK;
	}

	flags = DHD_PKTID_AUDIT_LOCK(pktid_map->pktid_audit_lock);

	handle = pktid_map->pktid_audit;
	if (handle == (struct bcm_mwbmap *)NULL) {
		DHD_ERROR((DHD_PKT_AUDIT_STR "Handle NULL\n", errmsg));
		goto out;
	}

	/* Exclude special pktids from audit */
	ignore_audit = (pktid == DHD_IOCTL_REQ_PKTID) | (pktid == DHD_FAKE_PKTID);
	if (ignore_audit) {
		goto out;
	}

	if ((pktid == DHD_PKTID_INVALID) || (pktid > pktid_map->items)) {
		DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> invalid\n", errmsg, pktid));
		error = BCME_ERROR;
		goto out;
	}

	/* Perform audit */
	switch (test_for) {
		case DHD_DUPLICATE_ALLOC:
			if (!bcm_mwbmap_isfree(handle, pktid)) {
				DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> alloc duplicate\n",
				           errmsg, pktid));
				error = BCME_ERROR;
			} else {
				bcm_mwbmap_force(handle, pktid);
			}
			break;

		case DHD_DUPLICATE_FREE:
			if (bcm_mwbmap_isfree(handle, pktid)) {
				DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> free duplicate\n",
				           errmsg, pktid));
				error = BCME_ERROR;
			} else {
				bcm_mwbmap_free(handle, pktid);
			}
			break;

		case DHD_TEST_IS_ALLOC:
			if (bcm_mwbmap_isfree(handle, pktid)) {
				DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> is not allocated\n",
				           errmsg, pktid));
				error = BCME_ERROR;
			}
			break;

		case DHD_TEST_IS_FREE:
			if (!bcm_mwbmap_isfree(handle, pktid)) {
				DHD_ERROR((DHD_PKT_AUDIT_STR "PktId<%d> is not free",
				           errmsg, pktid));
				error = BCME_ERROR;
			}
			break;

		default:
			DHD_ERROR(("%s: Invalid test case: %d\n", __FUNCTION__, test_for));
			error = BCME_ERROR;
			break;
	}

out:
	DHD_PKTID_AUDIT_UNLOCK(pktid_map->pktid_audit_lock, flags);

	if (error != BCME_OK) {
		dhd->pktid_audit_failed = TRUE;
	}

	return error;
}

static int
dhd_pktid_audit(dhd_pub_t *dhd, dhd_pktid_map_t *pktid_map, uint32 pktid,
	const int test_for, const char *errmsg)
{
	int ret = BCME_OK;
	ret = __dhd_pktid_audit(dhd, pktid_map, pktid, test_for, errmsg);
	if (ret == BCME_ERROR) {
		DHD_ERROR(("%s: Got Pkt Id Audit failure: PKTID<%d> PKTID MAP TYPE<%d>\n",
			__FUNCTION__, pktid, dhd_get_pktid_map_type(dhd, pktid_map)));
		dhd_pktid_error_handler(dhd);
#ifdef DHD_MAP_PKTID_LOGGING
		DHD_PKTID_LOG_DUMP(dhd);
#endif /* DHD_MAP_PKTID_LOGGING */
	}

	return ret;
}

#define DHD_PKTID_AUDIT(dhdp, map, pktid, test_for) \
	dhd_pktid_audit((dhdp), (dhd_pktid_map_t *)(map), (pktid), (test_for), __FUNCTION__)

static int
dhd_pktid_audit_ring_debug(dhd_pub_t *dhdp, dhd_pktid_map_t *map, uint32 pktid,
	const int test_for, void *msg, uint32 msg_len, const char *func)
{
	int ret = BCME_OK;

	if (dhd_query_bus_erros(dhdp)) {
		return BCME_ERROR;
	}

	ret = __dhd_pktid_audit(dhdp, map, pktid, test_for, func);
	if (ret == BCME_ERROR) {
		DHD_ERROR(("%s: Got Pkt Id Audit failure: PKTID<%d> PKTID MAP TYPE<%d>\n",
			__FUNCTION__, pktid, dhd_get_pktid_map_type(dhdp, map)));
		prhex(func, (uchar *)msg, msg_len);
		dhd_pktid_error_handler(dhdp);
#ifdef DHD_MAP_PKTID_LOGGING
		DHD_PKTID_LOG_DUMP(dhdp);
#endif /* DHD_MAP_PKTID_LOGGING */
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

	map->items = num_items;
	map->avail = num_items;

	map_items = DHD_PKIDMAP_ITEMS(map->items);

	map_keys_sz = DHD_PKTIDMAP_KEYS_SZ(map->items);

	/* Initialize the lock that protects this structure */
	map->pktid_lock = DHD_PKTID_LOCK_INIT(osh);
	if (map->pktid_lock == NULL) {
		DHD_ERROR(("%s:%d: Lock init failed \r\n", __FUNCTION__, __LINE__));
		goto error;
	}

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

		if (map->pktid_lock) {
			DHD_PKTID_LOCK_DEINIT(osh, map->pktid_lock);
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
	unsigned long flags;
	bool data_tx = FALSE;

	map = (dhd_pktid_map_t *)handle;
	DHD_PKTID_LOCK(map->pktid_lock, flags);
	osh = dhd->osh;

	map_items = DHD_PKIDMAP_ITEMS(map->items);
	/* skip reserved KEY #0, and start from 1 */

	for (nkey = 1; nkey <= map_items; nkey++) {
		if (map->lockers[nkey].state == LOCKER_IS_BUSY) {
			locker = &map->lockers[nkey];
			locker->state = LOCKER_IS_FREE;
			data_tx = (locker->pkttype == PKTTYPE_DATA_TX);
			if (data_tx) {
				OSL_ATOMIC_DEC(dhd->osh, &dhd->prot->active_tx_count);
			}

#ifdef DHD_PKTID_AUDIT_RING
			DHD_PKTID_AUDIT(dhd, map, nkey, DHD_DUPLICATE_FREE); /* duplicate frees */
#endif /* DHD_PKTID_AUDIT_RING */
#ifdef DHD_MAP_PKTID_LOGGING
			DHD_PKTID_LOG(dhd, dhd->prot->pktid_dma_unmap,
				locker->pa, nkey, locker->len,
				locker->pkttype);
#endif /* DHD_MAP_PKTID_LOGGING */

			DMA_UNMAP(osh, locker->pa, locker->len, locker->dir, 0, locker->dmah);
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
	DHD_PKTID_UNLOCK(map->pktid_lock, flags);
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
	unsigned long flags;

	map = (dhd_pktid_map_t *)handle;
	DHD_PKTID_LOCK(map->pktid_lock, flags);

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

			free_ioctl_return_buffer(dhd, &retbuf);
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
	DHD_PKTID_UNLOCK(map->pktid_lock, flags);
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

	if (handle == NULL)
		return;

	/* Free any pending packets */
	dhd_pktid_map_reset(dhd, handle);

	map = (dhd_pktid_map_t *)handle;
	dhd_pktid_map_sz = DHD_PKTID_MAP_SZ(map->items);
	map_keys_sz = DHD_PKTIDMAP_KEYS_SZ(map->items);

	DHD_PKTID_LOCK_DEINIT(dhd->osh, map->pktid_lock);

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

	if (handle == NULL)
		return;

	/* Free any pending packets */
	dhd_pktid_map_reset_ioctl(dhd, handle);

	map = (dhd_pktid_map_t *)handle;
	dhd_pktid_map_sz = DHD_PKTID_MAP_SZ(map->items);
	map_keys_sz = DHD_PKTIDMAP_KEYS_SZ(map->items);

	DHD_PKTID_LOCK_DEINIT(dhd->osh, map->pktid_lock);

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
static INLINE uint32
BCMFASTPATH(dhd_pktid_map_avail_cnt)(dhd_pktid_map_handle_t *handle)
{
	dhd_pktid_map_t *map;
	uint32	avail;
	unsigned long flags;

	ASSERT(handle != NULL);
	map = (dhd_pktid_map_t *)handle;

	DHD_PKTID_LOCK(map->pktid_lock, flags);
	avail = map->avail;
	DHD_PKTID_UNLOCK(map->pktid_lock, flags);

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
	unsigned long flags;

	ASSERT(handle != NULL);
	map = (dhd_pktid_map_t *)handle;

	DHD_PKTID_LOCK(map->pktid_lock, flags);

	if ((int)(map->avail) <= 0) { /* no more pktids to allocate */
		map->failures++;
		DHD_INFO(("%s:%d: failed, no free keys\n", __FUNCTION__, __LINE__));
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
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
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
		return DHD_PKTID_INVALID; /* failed alloc request */
	}

	locker = &map->lockers[nkey]; /* save packet metadata in locker */
	map->avail--;
	locker->pkt = pkt; /* pkt is saved, other params not yet saved. */
	locker->len = 0;
	locker->state = LOCKER_IS_BUSY; /* reserve this locker */

	DHD_PKTID_UNLOCK(map->pktid_lock, flags);

	ASSERT(nkey != DHD_PKTID_INVALID);

	return nkey; /* return locker's numbered key */
}

#ifdef DHD_PKTTS
/*
 * dhd_pktid_map_save_metadata - Save metadata information in a locker
 * that has a reserved unique numbered key.
 */
static INLINE void
dhd_pktid_map_save_metadata(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle, void *mpkt,
	dmaaddr_t mpkt_pa,
	uint16	mpkt_len,
	void *dmah,
	uint32 nkey)
{
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;
	unsigned long flags;

	ASSERT(handle != NULL);
	map = (dhd_pktid_map_t *)handle;

	DHD_PKTID_LOCK(map->pktid_lock, flags);

	if ((nkey == DHD_PKTID_INVALID) || (nkey > DHD_PKIDMAP_ITEMS(map->items))) {
		DHD_ERROR(("%s:%d: Error! saving invalid pktid<%u>",
			__FUNCTION__, __LINE__, nkey));
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
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

	/*
	 * TODO: checking the locker state for BUSY will prevent
	 * us from storing meta data on an already allocated
	 * Locker. But not checking may lead to overwriting
	 * existing data.
	 */
	locker->mpkt = mpkt;
	locker->mpkt_pa = mpkt_pa;
	locker->mpkt_len = mpkt_len;
	locker->dmah = dmah;

	DHD_PKTID_UNLOCK(map->pktid_lock, flags);
}
#endif /* DHD_PKTTS */

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
	unsigned long flags;

	ASSERT(handle != NULL);
	map = (dhd_pktid_map_t *)handle;

	DHD_PKTID_LOCK(map->pktid_lock, flags);

	if ((nkey == DHD_PKTID_INVALID) || (nkey > DHD_PKIDMAP_ITEMS(map->items))) {
		DHD_ERROR(("%s:%d: Error! saving invalid pktid<%u> pkttype<%u>\n",
			__FUNCTION__, __LINE__, nkey, pkttype));
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
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
#ifdef DHD_MAP_PKTID_LOGGING
	DHD_PKTID_LOG(dhd, dhd->prot->pktid_dma_map, pa, nkey, len, pkttype);
#endif /* DHD_MAP_PKTID_LOGGING */
	DHD_PKTID_UNLOCK(map->pktid_lock, flags);
}

/**
 * dhd_pktid_map_alloc - Allocate a unique numbered key and save the packet
 * contents into the corresponding locker. Return the numbered key.
 */
static uint32
BCMFASTPATH(dhd_pktid_map_alloc)(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle, void *pkt,
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

#ifdef DHD_PKTTS
static void *
BCMFASTPATH(dhd_pktid_map_retreive_metadata)(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle,
	dmaaddr_t *pmpkt_pa,
	uint32	*pmpkt_len,
	void **pdmah,
	uint32 nkey)
{
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;
	void *mpkt;
	unsigned long flags;

	ASSERT(handle != NULL);

	map = (dhd_pktid_map_t *)handle;

	DHD_PKTID_LOCK(map->pktid_lock, flags);

	/* XXX PLEASE DO NOT remove this ASSERT, fix the bug in caller. */
	if ((nkey == DHD_PKTID_INVALID) || (nkey > DHD_PKIDMAP_ITEMS(map->items))) {
		DHD_ERROR(("%s:%d: Error! Try to free invalid pktid<%u>\n",
		           __FUNCTION__, __LINE__, nkey));
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
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
	mpkt = locker->mpkt;
	*pmpkt_pa = locker->mpkt_pa;
	*pmpkt_len = locker->mpkt_len;
	if (pdmah)
		*pdmah = locker->dmah;
	locker->mpkt = NULL;
	locker->mpkt_len = 0;
	locker->dmah = NULL;

	DHD_PKTID_UNLOCK(map->pktid_lock, flags);
	return mpkt;
}
#endif /* DHD_PKTTS */

/**
 * dhd_pktid_map_free - Given a numbered key, return the locker contents.
 * dhd_pktid_map_free() is not reentrant, and is the caller's responsibility.
 * Caller may not free a pktid value DHD_PKTID_INVALID or an arbitrary pktid
 * value. Only a previously allocated pktid may be freed.
 */
static void *
BCMFASTPATH(dhd_pktid_map_free)(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle, uint32 nkey,
	dmaaddr_t *pa, uint32 *len, void **dmah, void **secdma, dhd_pkttype_t pkttype,
	bool rsv_locker)
{
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;
	void * pkt;
	unsigned long long locker_addr;
	unsigned long flags;

	ASSERT(handle != NULL);

	map = (dhd_pktid_map_t *)handle;

	DHD_PKTID_LOCK(map->pktid_lock, flags);

	/* XXX PLEASE DO NOT remove this ASSERT, fix the bug in caller. */
	if ((nkey == DHD_PKTID_INVALID) || (nkey > DHD_PKIDMAP_ITEMS(map->items))) {
		DHD_ERROR(("%s:%d: Error! Try to free invalid pktid<%u>, pkttype<%d>\n",
		           __FUNCTION__, __LINE__, nkey, pkttype));
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
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
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
		/* XXX PLEASE DO NOT remove this ASSERT, fix the bug in caller. */
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
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
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
#ifdef DHD_MAP_PKTID_LOGGING
	DHD_PKTID_LOG(dhd, dhd->prot->pktid_dma_unmap, locker->pa, nkey,
		(uint32)locker->len, pkttype);
#endif /* DHD_MAP_PKTID_LOGGING */

	*pa = locker->pa; /* return contents of locker */
	*len = (uint32)locker->len;
	*dmah = locker->dmah;
	*secdma = locker->secdma;

	pkt = locker->pkt;
	locker->pkt = NULL; /* Clear pkt */
	locker->len = 0;

	DHD_PKTID_UNLOCK(map->pktid_lock, flags);

	return pkt;
}

#else /* ! DHD_PCIE_PKTID */

#ifndef linux
#error "DHD_PCIE_PKTID has to be defined for non-linux/android platforms"
#endif

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
dhd_pktid_map_reset(dhd_pub_t *dhd, pktlists_t *handle)
{
	osl_t *osh = dhd->osh;

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
}

static void
dhd_pktid_map_fini(dhd_pub_t *dhd, dhd_pktid_map_handle_t *map)
{
	osl_t *osh = dhd->osh;
	pktlists_t *handle = (pktlists_t *) map;

	ASSERT(handle != NULL);
	if (handle == (pktlists_t *)NULL) {
		return;
	}

	dhd_pktid_map_reset(dhd, handle);

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

	/* XXX optimize these branch conditionals */
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

	/* XXX optimize these branch conditionals */
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

/*
 * Allocating buffers for common rings.
 * also allocating Buffers for hmaptest, Scratch buffer for dma rx offset,
 * bus_throughput_measurement and snapshot upload
 */
static int
dhd_prot_allocate_bufs(dhd_pub_t *dhd, dhd_prot_t *prot)
{

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

#ifdef DHD_HMAPTEST
	/* Allocate buffer for hmaptest  */
	DHD_ERROR(("allocating memory for hmaptest \n"));
	if (dhd_dma_buf_alloc(dhd, &prot->hmaptest.mem, HMAP_SANDBOX_BUFFER_LEN)) {

		goto fail;
	} else {
		uint32 scratch_len;
		uint64 scratch_lin, w1_start;
		dmaaddr_t scratch_pa;

		scratch_pa = prot->hmaptest.mem.pa;
		scratch_len = prot->hmaptest.mem.len;
		scratch_lin  = (uint64)(PHYSADDRLO(scratch_pa) & 0xffffffff)
			| (((uint64)PHYSADDRHI(scratch_pa)& 0xffffffff) << 32);
		w1_start  = scratch_lin +  scratch_len;
		DHD_ERROR(("hmap: NOTE Buffer alloc for HMAPTEST Start=0x%0llx len=0x%08x"
			"End=0x%0llx\n", (uint64) scratch_lin, scratch_len, (uint64) w1_start));
	}
#endif /* DHD_HMAPTEST */

	/* scratch buffer bus throughput measurement */
	if (dhd_dma_buf_alloc(dhd, &prot->host_bus_throughput_buf, DHD_BUS_TPUT_BUF_LEN)) {
		goto fail;
	}

#ifdef SNAPSHOT_UPLOAD
	/* snapshot upload buffer */
	if (dhd_dma_buf_alloc(dhd, &prot->snapshot_upload_buf, SNAPSHOT_UPLOAD_BUF_SIZE)) {
		goto fail;
	}
#endif	/* SNAPSHOT_UPLOAD */

	return BCME_OK;

fail:
	return BCME_NOMEM;
}

/**
 * The PCIE FD protocol layer is constructed in two phases:
 *    Phase 1. dhd_prot_attach()
 *    Phase 2. dhd_prot_init()
 *
 * dhd_prot_attach() - Allocates a dhd_prot_t object and resets all its fields.
 * All Common rings are also attached (msgbuf_ring_t objects are allocated
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
	uint32 trap_buf_len;

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
	dhd->dar_inited = 0;

	if (dhd_prot_allocate_bufs(dhd, prot) != BCME_OK) {
		goto fail;
	}

#ifdef DHD_RX_CHAINING
	dhd_rxchain_reset(&prot->rxchain);
#endif

	prot->pktid_ctrl_map = DHD_NATIVE_TO_PKTID_INIT(dhd, MAX_PKTID_CTRL);
	if (prot->pktid_ctrl_map == NULL) {
		goto fail;
	}

	prot->pktid_rx_map = DHD_NATIVE_TO_PKTID_INIT(dhd, MAX_PKTID_RX);
	if (prot->pktid_rx_map == NULL)
		goto fail;

	prot->pktid_tx_map = DHD_NATIVE_TO_PKTID_INIT(dhd, MAX_PKTID_TX);
	if (prot->pktid_rx_map == NULL)
		goto fail;

#ifdef IOCTLRESP_USE_CONSTMEM
	prot->pktid_map_handle_ioctl = DHD_NATIVE_TO_PKTID_INIT(dhd,
		DHD_FLOWRING_MAX_IOCTLRESPBUF_POST);
	if (prot->pktid_map_handle_ioctl == NULL) {
		goto fail;
	}
#endif /* IOCTLRESP_USE_CONSTMEM */

#ifdef DHD_MAP_PKTID_LOGGING
	prot->pktid_dma_map = DHD_PKTID_LOG_INIT(dhd, MAX_PKTID_LOG);
	if (prot->pktid_dma_map == NULL) {
		DHD_ERROR(("%s: failed to allocate pktid_dma_map\n",
			__FUNCTION__));
	}

	prot->pktid_dma_unmap = DHD_PKTID_LOG_INIT(dhd, MAX_PKTID_LOG);
	if (prot->pktid_dma_unmap == NULL) {
		DHD_ERROR(("%s: failed to allocate pktid_dma_unmap\n",
			__FUNCTION__));
	}
#endif /* DHD_MAP_PKTID_LOGGING */

#ifdef D2H_MINIDUMP
	if (dhd->bus->sih->buscorerev < 71) {
		trap_buf_len = BCMPCIE_HOST_EXT_TRAP_DBGBUF_LEN_MIN;
	} else {
		/* buscorerev >= 71, supports minimdump of len 96KB */
		trap_buf_len = BCMPCIE_HOST_EXT_TRAP_DBGBUF_LEN;
	}
#else
	/* FW going to DMA extended trap data,
	 * allocate buffer for the maximum extended trap data.
	 */
	trap_buf_len = BCMPCIE_EXT_TRAP_DATA_MAXLEN;
#endif /* D2H_MINIDUMP */

	/* Initialize trap buffer */
	if (dhd_dma_buf_alloc(dhd, &dhd->prot->fw_trap_buf, trap_buf_len)) {
		DHD_ERROR(("%s: dhd_init_trap_buffer falied\n", __FUNCTION__));
		goto fail;
	}

	return BCME_OK;

fail:

	if (prot) {
		/* Free up all allocated memories */
		dhd_prot_detach(dhd);
	}

	return BCME_NOMEM;
} /* dhd_prot_attach */

static int
dhd_alloc_host_scbs(dhd_pub_t *dhd)
{
	int ret = BCME_OK;
	sh_addr_t base_addr;
	dhd_prot_t *prot = dhd->prot;
	uint32 host_scb_size = 0;

	if (dhd->hscb_enable) {
		/* read number of bytes to allocate from F/W */
		dhd_bus_cmn_readshared(dhd->bus, &host_scb_size, HOST_SCB_ADDR, 0);
		if (host_scb_size) {
			/* In fw reload scenario the buffer could have been allocated for previous
			 * run. Check the existing buffer if there is one that can accommodate
			 * the new firmware requirement and reuse the buffer is possible.
			 */
			if (prot->host_scb_buf.va) {
				if (prot->host_scb_buf.len >= host_scb_size) {
					prot->host_scb_buf.len = host_scb_size;
				} else {
					dhd_dma_buf_free(dhd, &prot->host_scb_buf);
				}
			}
			/* alloc array of host scbs */
			if (prot->host_scb_buf.va == NULL) {
				ret = dhd_dma_buf_alloc(dhd, &prot->host_scb_buf, host_scb_size);
			}
			/* write host scb address to F/W */
			if (ret == BCME_OK) {
				dhd_base_addr_htolpa(&base_addr, prot->host_scb_buf.pa);
				dhd_bus_cmn_writeshared(dhd->bus, &base_addr, sizeof(base_addr),
					HOST_SCB_ADDR, 0);
			}
		}
	} else {
		DHD_TRACE(("%s: Host scb not supported in F/W. \n", __FUNCTION__));
	}

	if (ret != BCME_OK) {
		DHD_ERROR(("%s dhd_alloc_host_scbs, alloc failed: Err Code %d\n",
			__FUNCTION__, ret));
	}
	return ret;
}

void
dhd_set_host_cap(dhd_pub_t *dhd)
{
	uint32 data = 0;
	dhd_prot_t *prot = dhd->prot;
#ifdef D2H_MINIDUMP
	uint16 host_trap_addr_len;
#endif /* D2H_MINIDUMP */

	if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_6) {
		if (dhd->h2d_phase_supported) {
			data |= HOSTCAP_H2D_VALID_PHASE;
			if (dhd->force_dongletrap_on_bad_h2d_phase)
				data |= HOSTCAP_H2D_ENABLE_TRAP_ON_BADPHASE;
		}
		if (prot->host_ipc_version > prot->device_ipc_version)
			prot->active_ipc_version = prot->device_ipc_version;
		else
			prot->active_ipc_version = prot->host_ipc_version;

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
			if (!dhd->dma_h2d_ring_upd_support || !dhd->dma_d2h_ring_upd_support) {
				dhd_init_dongle_ds_lock(dhd->bus);
				dhdpcie_set_dongle_deepsleep(dhd->bus, FALSE);
			}
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

		/* Indicate support for extended trap data */
		data |= HOSTCAP_EXTENDED_TRAP_DATA;

		/* Indicate support for TX status metadata */
		if (dhd->pcie_txs_metadata_enable != 0)
			data |= HOSTCAP_TXSTATUS_METADATA;

#ifdef BTLOG
		/* Indicate support for BT logging */
		if (dhd->bt_logging) {
			if (dhd->bt_logging_enabled) {
				data |= HOSTCAP_BT_LOGGING;
				DHD_ERROR(("BT LOGGING  enabled\n"));
			}
			else {
				DHD_ERROR(("BT logging upported in FW, BT LOGGING disabled\n"));
			}
		}
		else {
			DHD_ERROR(("BT LOGGING not enabled in FW !!\n"));
		}
#endif	/* BTLOG */

		/* Enable fast delete ring in firmware if supported */
		if (dhd->fast_delete_ring_support) {
			data |= HOSTCAP_FAST_DELETE_RING;
		}

		if (dhdpcie_bus_get_pcie_idma_supported(dhd->bus)) {
			DHD_ERROR(("IDMA inited\n"));
			data |= HOSTCAP_H2D_IDMA;
			dhd->idma_inited = TRUE;
		} else {
			DHD_ERROR(("IDMA not enabled in FW !!\n"));
			dhd->idma_inited = FALSE;
		}

		if (dhdpcie_bus_get_pcie_ifrm_supported(dhd->bus)) {
			DHD_ERROR(("IFRM Inited\n"));
			data |= HOSTCAP_H2D_IFRM;
			dhd->ifrm_inited = TRUE;
			dhd->dma_h2d_ring_upd_support = FALSE;
			dhd_prot_dma_indx_free(dhd);
		} else {
			DHD_ERROR(("IFRM not enabled in FW !!\n"));
			dhd->ifrm_inited = FALSE;
		}

		if (dhdpcie_bus_get_pcie_dar_supported(dhd->bus)) {
			DHD_ERROR(("DAR doorbell Use\n"));
			data |= HOSTCAP_H2D_DAR;
			dhd->dar_inited = TRUE;
		} else {
			DHD_ERROR(("DAR not enabled in FW !!\n"));
			dhd->dar_inited = FALSE;
		}

		/* FW Checks for HOSTCAP_UR_FW_NO_TRAP and Does not TRAP if set
		 * Radar 36403220 JIRA SWWLAN-182145
		 */
		data |= HOSTCAP_UR_FW_NO_TRAP;

#ifdef SNAPSHOT_UPLOAD
		/* Indicate support for snapshot upload */
		if (dhd->snapshot_upload) {
			data |= HOSTCAP_SNAPSHOT_UPLOAD;
			DHD_ERROR(("ALLOW SNAPSHOT UPLOAD!!\n"));
		}
#endif	/* SNAPSHOT_UPLOAD */

		if (dhd->hscb_enable) {
			data |= HOSTCAP_HSCB;
		}

#ifdef EWP_EDL
		if (dhd->dongle_edl_support) {
			data |= HOSTCAP_EDL_RING;
			DHD_ERROR(("Enable EDL host cap\n"));
		} else {
			DHD_ERROR(("DO NOT SET EDL host cap\n"));
		}
#endif /* EWP_EDL */

#ifdef D2H_MINIDUMP
		if (dhd_bus_is_minidump_enabled(dhd)) {
			data |= HOSTCAP_EXT_TRAP_DBGBUF;
			DHD_ERROR(("ALLOW D2H MINIDUMP!!\n"));
		}
#endif /* D2H_MINIDUMP */
#ifdef DHD_HP2P
		if (dhdpcie_bus_get_hp2p_supported(dhd->bus)) {
			data |= HOSTCAP_PKT_TIMESTAMP;
			data |= HOSTCAP_PKT_HP2P;
			DHD_ERROR(("Enable HP2P in host cap\n"));
		} else {
			DHD_ERROR(("HP2P not enabled in host cap\n"));
		}
#endif /* DHD_HP2P */

#ifdef DHD_DB0TS
		if (dhd->db0ts_capable) {
			data |= HOSTCAP_DB0_TIMESTAMP;
			DHD_ERROR(("Enable DB0 TS in host cap\n"));
		} else {
			DHD_ERROR(("DB0 TS not enabled in host cap\n"));
		}
#endif /* DHD_DB0TS */
		if (dhd->extdtxs_in_txcpl) {
			DHD_ERROR(("Enable hostcap: EXTD TXS in txcpl\n"));
			data |= HOSTCAP_PKT_TXSTATUS;
		}
		else {
			DHD_ERROR(("Enable hostcap: EXTD TXS in txcpl\n"));
		}

		DHD_INFO(("%s:Active Ver:%d, Host Ver:%d, FW Ver:%d\n",
			__FUNCTION__,
			prot->active_ipc_version, prot->host_ipc_version,
			prot->device_ipc_version));

		dhd_bus_cmn_writeshared(dhd->bus, &data, sizeof(uint32), HOST_API_VERSION, 0);
		dhd_bus_cmn_writeshared(dhd->bus, &prot->fw_trap_buf.pa,
			sizeof(prot->fw_trap_buf.pa), DNGL_TO_HOST_TRAP_ADDR, 0);
#ifdef D2H_MINIDUMP
		if (dhd_bus_is_minidump_enabled(dhd)) {
			/* Dongle expects the host_trap_addr_len in terms of words */
			host_trap_addr_len = prot->fw_trap_buf.len / 4;
			dhd_bus_cmn_writeshared(dhd->bus, &host_trap_addr_len,
				sizeof(host_trap_addr_len), DNGL_TO_HOST_TRAP_ADDR_LEN, 0);
		}
#endif /* D2H_MINIDUMP */
	}

#ifdef DHD_TIMESYNC
	dhd_timesync_notify_ipc_rev(dhd->ts, prot->active_ipc_version);
#endif /* DHD_TIMESYNC */
}

#ifdef AGG_H2D_DB
void dhd_agg_inflight_stats_dump(dhd_pub_t *dhd, struct bcmstrbuf *strbuf)
{
	uint64 *inflight_histo = dhd->prot->agg_h2d_db_info.inflight_histo;
	uint32 i;
	uint64 total_inflight_histo = 0;

	bcm_bprintf(strbuf, "inflight: \t count\n");
	for (i = 0; i < DHD_NUM_INFLIGHT_HISTO_ROWS; i++) {
		bcm_bprintf(strbuf, "%16u: \t %llu\n", 1U<<i, inflight_histo[i]);
		total_inflight_histo += inflight_histo[i];
	}
	bcm_bprintf(strbuf, "total_inflight_histo: %llu\n", total_inflight_histo);
}

void dhd_agg_inflights_stats_update(dhd_pub_t *dhd, uint32 inflight)
{
	uint64 *bin = dhd->prot->agg_h2d_db_info.inflight_histo;
	uint64 *p;
	uint32 bin_power;
	bin_power = next_larger_power2(inflight);

	switch (bin_power) {
		case   1: p = bin + 0; break;
		case   2: p = bin + 1; break;
		case   4: p = bin + 2; break;
		case   8: p = bin + 3; break;
		case  16: p = bin + 4; break;
		case  32: p = bin + 5; break;
		case  64: p = bin + 6; break;
		case 128: p = bin + 7; break;
		case 256: p = bin + 8; break;
		case 512: p = bin + 9; break;
		case 1024: p = bin + 10; break;
		case 2048: p = bin + 11; break;
		case 4096: p = bin + 12; break;
		case 8192: p = bin + 13; break;
		default : p = bin + 13; break;
	}
	ASSERT((p - bin) < DHD_NUM_INFLIGHT_HISTO_ROWS);
	*p = *p + 1;
	return;
}

/*
 * dhd_msgbuf_agg_h2d_db_timer_fn:
 * Timer callback function for ringing h2d DB.
 * This is run in isr context (HRTIMER_MODE_REL),
 * do not hold any spin_lock_bh().
 * Using HRTIMER_MODE_REL_SOFT causing TPUT regressions.
 */
enum hrtimer_restart
dhd_msgbuf_agg_h2d_db_timer_fn(struct hrtimer *timer)
{
	agg_h2d_db_info_t *agg_db_info;
	dhd_pub_t *dhd;
	dhd_prot_t *prot;
	uint32 db_index;
	uint corerev;

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	agg_db_info = container_of(timer, agg_h2d_db_info_t, timer);
	GCC_DIAGNOSTIC_POP();

	dhd = agg_db_info->dhd;
	prot = dhd->prot;

	prot->agg_h2d_db_info.timer_db_cnt++;
	if (IDMA_ACTIVE(dhd)) {
		db_index = IDMA_IDX0;
		if (dhd->bus->sih) {
			corerev = dhd->bus->sih->buscorerev;
			if (corerev >= 24) {
				db_index |= (DMA_TYPE_IDMA << DMA_TYPE_SHIFT);
			}
		}
		prot->mb_2_ring_fn(dhd->bus, db_index, TRUE);
	} else {
		prot->mb_ring_fn(dhd->bus, DHD_AGGR_H2D_DB_MAGIC);
	}

	return HRTIMER_NORESTART;
}

void
dhd_msgbuf_agg_h2d_db_timer_start(dhd_prot_t *prot)
{
	agg_h2d_db_info_t *agg_db_info = &prot->agg_h2d_db_info;

	/* Queue the timer only when it is not in the queue */
	if (!hrtimer_active(&agg_db_info->timer)) {
		hrtimer_start(&agg_db_info->timer, ns_to_ktime(agg_h2d_db_timeout * NSEC_PER_USEC),
				HRTIMER_MODE_REL);
	}
}

static void
dhd_msgbuf_agg_h2d_db_timer_init(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	agg_h2d_db_info_t *agg_db_info = &prot->agg_h2d_db_info;

	agg_db_info->dhd = dhd;
	hrtimer_init(&agg_db_info->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	/* The timer function will run from ISR context, ensure no spin_lock_bh are used */
	agg_db_info->timer.function = &dhd_msgbuf_agg_h2d_db_timer_fn;
	agg_db_info->init = TRUE;
	agg_db_info->timer_db_cnt = 0;
	agg_db_info->direct_db_cnt = 0;
	agg_db_info->inflight_histo = (uint64 *)MALLOCZ(dhd->osh, DHD_INFLIGHT_HISTO_SIZE);
}

static void
dhd_msgbuf_agg_h2d_db_timer_reset(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	agg_h2d_db_info_t *agg_db_info = &prot->agg_h2d_db_info;
	if (agg_db_info->init) {
		if (agg_db_info->inflight_histo) {
			MFREE(dhd->osh, agg_db_info->inflight_histo, DHD_INFLIGHT_HISTO_SIZE);
		}
		hrtimer_try_to_cancel(&agg_db_info->timer);
		agg_db_info->init = FALSE;
	}
}

static void
dhd_msgbuf_agg_h2d_db_timer_cancel(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	agg_h2d_db_info_t *agg_db_info = &prot->agg_h2d_db_info;
	hrtimer_try_to_cancel(&agg_db_info->timer);
}
#endif /* AGG_H2D_DB */

void
dhd_prot_clearcounts(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
#ifdef AGG_H2D_DB
	agg_h2d_db_info_t *agg_db_info = &prot->agg_h2d_db_info;
	if (agg_db_info->inflight_histo) {
		memset(agg_db_info->inflight_histo, 0, DHD_INFLIGHT_HISTO_SIZE);
	}
	agg_db_info->direct_db_cnt = 0;
	agg_db_info->timer_db_cnt = 0;
#endif /* AGG_H2D_DB */
	prot->txcpl_db_cnt = 0;
	prot->tx_h2d_db_cnt = 0;
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
	uint32 idmacontrol;
	uint32 waitcount = 0;
	uint16 max_eventbufpost = 0;

	/**
	 * A user defined value can be assigned to global variable h2d_max_txpost via
	 * 1. DHD IOVAR h2d_max_txpost, before firmware download
	 * 2. module parameter h2d_max_txpost
	 * prot->h2d_max_txpost is assigned with DHD_H2DRING_TXPOST_MAX_ITEM,
	 * if user has not defined any buffers by one of the above methods.
	 */
	prot->h2d_max_txpost = (uint16)h2d_max_txpost;
	DHD_ERROR(("%s:%d: h2d_max_txpost = %d\n", __FUNCTION__, __LINE__, prot->h2d_max_txpost));

#if defined(DHD_HTPUT_TUNABLES)
	prot->h2d_htput_max_txpost = (uint16)h2d_htput_max_txpost;
	DHD_ERROR(("%s:%d: h2d_htput_max_txpost = %d\n",
		__FUNCTION__, __LINE__, prot->h2d_htput_max_txpost));
#endif /* DHD_HTPUT_TUNABLES */

	/* Read max rx packets supported by dongle */
	dhd_bus_cmn_readshared(dhd->bus, &prot->max_rxbufpost, MAX_HOST_RXBUFS, 0);
	if (prot->max_rxbufpost == 0) {
		/* This would happen if the dongle firmware is not */
		/* using the latest shared structure template */
		prot->max_rxbufpost = DEFAULT_RX_BUFFERS_TO_POST;
	}
	DHD_ERROR(("%s:%d: MAX_RXBUFPOST = %d\n", __FUNCTION__, __LINE__, prot->max_rxbufpost));

	/* Initialize.  bzero() would blow away the dma pointers. */
	max_eventbufpost = (uint16)dhdpcie_get_max_eventbufpost(dhd->bus);
	prot->max_eventbufpost = (((max_eventbufpost + DHD_FLOWRING_MAX_IOCTLRESPBUF_POST)) >=
		H2DRING_CTRL_SUB_MAX_ITEM) ? DHD_FLOWRING_MAX_EVENTBUF_POST : max_eventbufpost;
	prot->max_ioctlrespbufpost = DHD_FLOWRING_MAX_IOCTLRESPBUF_POST;
	prot->max_infobufpost = DHD_H2D_INFORING_MAX_BUF_POST;
#ifdef BTLOG
	prot->max_btlogbufpost = DHD_H2D_BTLOGRING_MAX_BUF_POST;
#endif	/* BTLOG */
	prot->max_tsbufpost = DHD_MAX_TSBUF_POST;

	prot->cur_ioctlresp_bufs_posted = 0;
	OSL_ATOMIC_INIT(dhd->osh, &prot->active_tx_count);
	prot->data_seq_no = 0;
	prot->ioctl_seq_no = 0;
	prot->rxbufpost = 0;
	prot->tot_rxbufpost = 0;
	prot->tot_rxcpl = 0;
	prot->cur_event_bufs_posted = 0;
	prot->ioctl_state = 0;
	prot->curr_ioctl_cmd = 0;
	prot->cur_ts_bufs_posted = 0;
	prot->infobufpost = 0;
#ifdef BTLOG
	prot->btlogbufpost = 0;
#endif	/* BTLOG */

	prot->dmaxfer.srcmem.va = NULL;
	prot->dmaxfer.dstmem.va = NULL;
	prot->dmaxfer.in_progress = FALSE;

#ifdef DHD_HMAPTEST
	prot->hmaptest.in_progress = FALSE;
#endif /* DHD_HMAPTEST */
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

	/* Initialize Common MsgBuf Rings */

	prot->device_ipc_version = dhd->bus->api.fw_rev;
	prot->host_ipc_version = PCIE_SHARED_VERSION;
	prot->no_tx_resource = FALSE;

	/* Init the host API version */
	dhd_set_host_cap(dhd);

	/* alloc and configure scb host address for dongle */
	if ((ret = dhd_alloc_host_scbs(dhd))) {
		return ret;
	}

	/* Register the interrupt function upfront */
	/* remove corerev checks in data path */
	/* do this after host/fw negotiation for DAR */
	prot->mb_ring_fn = dhd_bus_get_mbintr_fn(dhd->bus);
	prot->mb_2_ring_fn = dhd_bus_get_mbintr_2_fn(dhd->bus);

	prot->tx_h2d_db_cnt = 0;
#ifdef AGG_H2D_DB
	dhd_msgbuf_agg_h2d_db_timer_init(dhd);
#endif /* AGG_H2D_DB */

	dhd->bus->_dar_war = (dhd->bus->sih->buscorerev < 64) ? TRUE : FALSE;

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
#ifdef DHD_DMA_INDICES_SEQNUM
	prot->host_seqnum = D2H_EPOCH_INIT_VAL % D2H_EPOCH_MODULO;
#endif /* DHD_DMA_INDICES_SEQNUM */
	/* Signal to the dongle that common ring init is complete */
	if (dhd->hostrdy_after_init)
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

	dhd->ring_attached = TRUE;

	/* If IFRM is enabled, wait for FW to setup the DMA channel */
	if (IFRM_ENAB(dhd)) {
		dhd_base_addr_htolpa(&base_addr, prot->h2d_ifrm_indx_wr_buf.pa);
		dhd_bus_cmn_writeshared(dhd->bus, &base_addr, sizeof(base_addr),
			H2D_IFRM_INDX_WR_BUF, 0);
	}

	/* If IDMA is enabled and initied, wait for FW to setup the IDMA descriptors
	 * Waiting just before configuring doorbell
	 */
#ifdef BCMQT
#define	IDMA_ENABLE_WAIT  100
#else
#define	IDMA_ENABLE_WAIT  10
#endif
	if (IDMA_ACTIVE(dhd)) {
		/* wait for idma_en bit in IDMAcontrol register to be set */
		/* Loop till idma_en is not set */
		uint buscorerev = dhd->bus->sih->buscorerev;
		idmacontrol = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			IDMAControl(buscorerev), 0, 0);
		while (!(idmacontrol & PCIE_IDMA_MODE_EN(buscorerev)) &&
			(waitcount++ < IDMA_ENABLE_WAIT)) {

			DHD_ERROR(("iDMA not enabled yet,waiting 1 ms c=%d IDMAControl = %08x\n",
				waitcount, idmacontrol));
			OSL_DELAY(1000); /* 1ms as its onetime only */
			idmacontrol = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
				IDMAControl(buscorerev), 0, 0);
		}

		if (waitcount < IDMA_ENABLE_WAIT) {
			DHD_ERROR(("iDMA enabled PCIEControl = %08x\n", idmacontrol));
		} else {
			DHD_ERROR(("Error: wait for iDMA timed out wait=%d IDMAControl = %08x\n",
				waitcount, idmacontrol));
			return BCME_ERROR;
		}
		// add delay to fix bring up issue
		OSL_SLEEP(1);
	}

	/* Host should configure soft doorbells if needed ... here */

	/* Post to dongle host configured soft doorbells */
	dhd_msgbuf_ring_config_d2h_soft_doorbell(dhd);

	dhd_msgbuf_rxbuf_post_ioctlresp_bufs(dhd);
	dhd_msgbuf_rxbuf_post_event_bufs(dhd);

	prot->no_retry = FALSE;
	prot->no_aggr = FALSE;
	prot->fixed_rate = FALSE;

	/*
	 * Note that any communication with the Dongle should be added
	 * below this point. Any other host data structure initialiation that
	 * needs to be done prior to the DPC starts executing should be done
	 * befor this point.
	 * Because once we start sending H2D requests to Dongle, the Dongle
	 * respond immediately. So the DPC context to handle this
	 * D2H response could preempt the context in which dhd_prot_init is running.
	 * We want to ensure that all the Host part of dhd_prot_init is
	 * done before that.
	 */

	/* See if info rings could be created, info rings should be created
	* only if dongle does not support EDL
	*/
#ifdef EWP_EDL
	if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_6 && !dhd->dongle_edl_support) {
#else
	if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_6) {
#endif /* EWP_EDL */
		if ((ret = dhd_prot_init_info_rings(dhd)) != BCME_OK) {
			/* For now log and proceed, further clean up action maybe necessary
			 * when we have more clarity.
			 */
			DHD_ERROR(("%s Info rings couldn't be created: Err Code%d",
				__FUNCTION__, ret));
		}
	}

#ifdef EWP_EDL
		/* Create Enhanced Debug Lane rings (EDL) if dongle supports it */
		if (dhd->dongle_edl_support) {
			if ((ret = dhd_prot_init_edl_rings(dhd)) != BCME_OK) {
				DHD_ERROR(("%s EDL rings couldn't be created: Err Code%d",
					__FUNCTION__, ret));
			}
		}
#endif /* EWP_EDL */

#ifdef BTLOG
	/* create BT log rings */
	if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_7 && dhd->bt_logging) {
		if ((ret = dhd_prot_init_btlog_rings(dhd)) != BCME_OK) {
			/* For now log and proceed, further clean up action maybe necessary
			 * when we have more clarity.
			 */
			DHD_ERROR(("%s Info rings couldn't be created: Err Code%d",
				__FUNCTION__, ret));
		}
	}
#endif	/* BTLOG */

#ifdef DHD_HP2P
	/* create HPP txcmpl/rxcmpl rings */
	if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_7 && dhd->hp2p_capable) {
		if ((ret = dhd_prot_init_hp2p_rings(dhd)) != BCME_OK) {
			/* For now log and proceed, further clean up action maybe necessary
			 * when we have more clarity.
			 */
			DHD_ERROR(("%s HP2P rings couldn't be created: Err Code%d",
				__FUNCTION__, ret));
		}
	}
#endif /* DHD_HP2P */

#ifdef DHD_LB_RXP
	/* defualt rx flow ctrl thresholds. Can be changed at run time through sysfs */
	dhd->lb_rxp_stop_thr = (D2HRING_RXCMPLT_MAX_ITEM * LB_RXP_STOP_THR);
	dhd->lb_rxp_strt_thr = (D2HRING_RXCMPLT_MAX_ITEM * LB_RXP_STRT_THR);
	atomic_set(&dhd->lb_rxp_flow_ctrl, FALSE);
#endif /* DHD_LB_RXP */
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
		/* For non-android platforms, devreset will not be called,
		 * so call prot_reset here. It is harmless if called twice.
		 */
		dhd_prot_reset(dhd);

		/* free up all DMA-able buffers allocated during prot attach/init */

		dhd_dma_buf_free(dhd, &prot->d2h_dma_scratch_buf);
#ifdef DHD_HMAPTEST
		dhd_dma_buf_free(dhd, &prot->hmaptest.mem);
#endif /* DHD_HMAPTEST */
		dhd_dma_buf_free(dhd, &prot->retbuf);
		dhd_dma_buf_free(dhd, &prot->ioctbuf);
		dhd_dma_buf_free(dhd, &prot->host_bus_throughput_buf);
		dhd_dma_buf_free(dhd, &prot->hostts_req_buf);
		dhd_dma_buf_free(dhd, &prot->fw_trap_buf);
		dhd_dma_buf_free(dhd, &prot->host_scb_buf);
#ifdef SNAPSHOT_UPLOAD
		dhd_dma_buf_free(dhd, &prot->snapshot_upload_buf);
#endif	/* SNAPSHOT_UPLOAD */

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

#ifdef BTLOG
		/* detach BT log rings */
		dhd_prot_detach_btlog_rings(dhd);
#endif	/* BTLOG */

#ifdef EWP_EDL
		dhd_prot_detach_edl_rings(dhd);
#endif
#ifdef DHD_HP2P
		/* detach HPP rings */
		dhd_prot_detach_hp2p_rings(dhd);
#endif /* DHD_HP2P */

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
#ifdef DHD_MAP_PKTID_LOGGING
		DHD_PKTID_LOG_FINI(dhd, prot->pktid_dma_map);
		DHD_PKTID_LOG_FINI(dhd, prot->pktid_dma_unmap);
#endif /* DHD_MAP_PKTID_LOGGING */
#ifdef DHD_DMA_INDICES_SEQNUM
		if (prot->h2d_dma_indx_rd_copy_buf) {
			MFREE(dhd->osh, prot->h2d_dma_indx_rd_copy_buf,
				prot->h2d_dma_indx_rd_copy_bufsz);
		}
		if (prot->d2h_dma_indx_wr_copy_buf) {
			MFREE(dhd->osh, prot->d2h_dma_indx_wr_copy_buf,
				prot->d2h_dma_indx_wr_copy_bufsz);
		}
#endif /* DHD_DMA_INDICES_SEQNUM */
		DHD_OS_PREFREE(dhd, dhd->prot, sizeof(dhd_prot_t));

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

	dhd->ring_attached = FALSE;

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

#ifdef EWP_EDL
	if (prot->d2hring_edl) {
		dhd_prot_ring_reset(dhd, prot->d2hring_edl);
	}
#endif /* EWP_EDL */

	/* Reset all DMA-able buffers allocated during prot attach */
	dhd_dma_buf_reset(dhd, &prot->d2h_dma_scratch_buf);
#ifdef DHD_HMAPTEST
	dhd_dma_buf_reset(dhd, &prot->hmaptest.mem);
#endif /* DHD_HMAPTEST */
	dhd_dma_buf_reset(dhd, &prot->retbuf);
	dhd_dma_buf_reset(dhd, &prot->ioctbuf);
	dhd_dma_buf_reset(dhd, &prot->host_bus_throughput_buf);
	dhd_dma_buf_reset(dhd, &prot->hostts_req_buf);
	dhd_dma_buf_reset(dhd, &prot->fw_trap_buf);
	dhd_dma_buf_reset(dhd, &prot->host_scb_buf);
#ifdef SNAPSHOT_UPLOAD
	dhd_dma_buf_reset(dhd, &prot->snapshot_upload_buf);
#endif /* SNAPSHOT_UPLOAD */

	dhd_dma_buf_reset(dhd, &prot->h2d_ifrm_indx_wr_buf);

	/* Rest all DMA-able buffers for DMAing H2D/D2H WR/RD indices */
	dhd_dma_buf_reset(dhd, &prot->h2d_dma_indx_rd_buf);
	dhd_dma_buf_reset(dhd, &prot->h2d_dma_indx_wr_buf);
	dhd_dma_buf_reset(dhd, &prot->d2h_dma_indx_rd_buf);
	dhd_dma_buf_reset(dhd, &prot->d2h_dma_indx_wr_buf);

#ifdef DHD_DMA_INDICES_SEQNUM
		if (prot->d2h_dma_indx_wr_copy_buf) {
			dhd_local_buf_reset(prot->h2d_dma_indx_rd_copy_buf,
				prot->h2d_dma_indx_rd_copy_bufsz);
			dhd_local_buf_reset(prot->d2h_dma_indx_wr_copy_buf,
				prot->d2h_dma_indx_wr_copy_bufsz);
		}
#endif /* DHD_DMA_INDICES_SEQNUM */

	/* XXX: dmaxfer src and dst? */

	prot->rx_metadata_offset = 0;
	prot->tx_metadata_offset = 0;

	prot->rxbufpost = 0;
	prot->cur_event_bufs_posted = 0;
	prot->cur_ioctlresp_bufs_posted = 0;

	OSL_ATOMIC_INIT(dhd->osh, &prot->active_tx_count);
	prot->data_seq_no = 0;
	prot->ioctl_seq_no = 0;
	prot->ioctl_state = 0;
	prot->curr_ioctl_cmd = 0;
	prot->ioctl_received = IOCTL_WAIT;
	/* To catch any rollover issues fast, starting with higher ioctl_trans_id */
	prot->ioctl_trans_id = MAXBITVAL(NBITS(prot->ioctl_trans_id)) - BUFFER_BEFORE_ROLLOVER;
	prot->txcpl_db_cnt = 0;

	/* dhd_flow_rings_init is located at dhd_bus_start,
	 * so when stopping bus, flowrings shall be deleted
	 */
	if (dhd->flow_rings_inited) {
		dhd_flow_rings_deinit(dhd);
	}

#ifdef BTLOG
	/* Reset BTlog rings */
	if (prot->h2dring_btlog_subn) {
		dhd_prot_ring_reset(dhd, prot->h2dring_btlog_subn);
	}

	if (prot->d2hring_btlog_cpln) {
		dhd_prot_ring_reset(dhd, prot->d2hring_btlog_cpln);
	}
#endif	/* BTLOG */
#ifdef DHD_HP2P
	if (prot->d2hring_hp2p_txcpl) {
		dhd_prot_ring_reset(dhd, prot->d2hring_hp2p_txcpl);
	}
	if (prot->d2hring_hp2p_rxcpl) {
		dhd_prot_ring_reset(dhd, prot->d2hring_hp2p_rxcpl);
	}
#endif /* DHD_HP2P */

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

#ifdef AGG_H2D_DB
	dhd_msgbuf_agg_h2d_db_timer_reset(dhd);
#endif /* AGG_H2D_DB */

} /* dhd_prot_reset */

#if defined(DHD_LB_RXP)
#define DHD_LB_DISPATCH_RX_PROCESS(dhdp)	dhd_lb_dispatch_rx_process(dhdp)
#else /* !DHD_LB_RXP */
#define DHD_LB_DISPATCH_RX_PROCESS(dhdp)	do { /* noop */ } while (0)
#endif /* !DHD_LB_RXP */

#if defined(DHD_LB)
/* DHD load balancing: deferral of work to another online CPU */
/* DHD_LB_RXP dispatchers, in dhd_linux.c */
extern void dhd_lb_rx_napi_dispatch(dhd_pub_t *dhdp);
extern void dhd_lb_rx_pkt_enqueue(dhd_pub_t *dhdp, void *pkt, int ifidx);
extern unsigned long dhd_read_lb_rxp(dhd_pub_t *dhdp);

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
	uint16 ringid;

#ifdef BTLOG
	if (dhd->submit_count_WAR) {
		ringid = dhd->bus->max_tx_flowrings + BCMPCIE_COMMON_MSGRINGS;
	} else
#endif	/* BTLOG */
	{
		/* dongle may increase max_submission_rings so keep
		 * ringid at end of dynamic rings
		 */
		ringid = dhd->bus->max_tx_flowrings +
			(dhd->bus->max_submission_rings - dhd->bus->max_tx_flowrings) +
			BCMPCIE_H2D_COMMON_MSGRINGS;
	}

	if (prot->d2hring_info_cpln) {
		/* for d2hring re-entry case, clear inited flag */
		prot->d2hring_info_cpln->inited = FALSE;
	}

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

	if (prot->d2hring_info_cpln) {
		MFREE(prot->osh, prot->d2hring_info_cpln, sizeof(msgbuf_ring_t));
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
	ret = dhd_send_d2h_ringcreate(dhd, prot->d2hring_info_cpln,
		BCMPCIE_D2H_RING_TYPE_DBGBUF_CPL, DHD_D2H_DBGRING_REQ_PKTID);
	if (ret != BCME_OK)
		return ret;

	prot->h2dring_info_subn->seqnum = H2D_EPOCH_INIT_VAL;
	prot->h2dring_info_subn->current_phase = 0;
	prot->d2hring_info_cpln->seqnum = D2H_EPOCH_INIT_VAL;
	prot->d2hring_info_cpln->current_phase = BCMPCIE_CMNHDR_PHASE_BIT_INIT;

	DHD_TRACE(("trying to send create h2d info ring id %d\n", prot->h2dring_info_subn->idx));
	prot->h2dring_info_subn->n_completion_ids = 1;
	prot->h2dring_info_subn->compeltion_ring_ids[0] = prot->d2hring_info_cpln->idx;

	ret = dhd_send_h2d_ringcreate(dhd, prot->h2dring_info_subn,
		BCMPCIE_H2D_RING_TYPE_DBGBUF_SUBMIT, DHD_H2D_DBGRING_REQ_PKTID);

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
	}
	if (dhd->prot->d2hring_info_cpln) {
		dhd_prot_ring_detach(dhd, dhd->prot->d2hring_info_cpln);
		MFREE(dhd->prot->osh, dhd->prot->d2hring_info_cpln, sizeof(msgbuf_ring_t));
	}
}

#ifdef DHD_HP2P
static int
dhd_check_create_hp2p_rings(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = BCME_ERROR;
	uint16 ringid;

	/* Last 2 dynamic ring indices are used by hp2p rings */
	ringid = dhd->bus->max_submission_rings + dhd->bus->max_completion_rings - 2;

	if (prot->d2hring_hp2p_txcpl == NULL) {
		prot->d2hring_hp2p_txcpl = MALLOCZ(prot->osh, sizeof(msgbuf_ring_t));

		if (prot->d2hring_hp2p_txcpl == NULL) {
			DHD_ERROR(("%s: couldn't alloc memory for d2hring_hp2p_txcpl\n",
				__FUNCTION__));
			return BCME_NOMEM;
		}

		DHD_INFO(("%s: about to create hp2p txcpl ring\n", __FUNCTION__));
		ret = dhd_prot_ring_attach(dhd, prot->d2hring_hp2p_txcpl, "d2hhp2p_txcpl",
			dhd_bus_get_hp2p_ring_max_size(dhd->bus, TRUE), D2HRING_TXCMPLT_ITEMSIZE,
			ringid);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: couldn't alloc resources for hp2p txcpl ring\n",
				__FUNCTION__));
			goto err2;
		}
	} else {
		/* for re-entry case, clear inited flag */
		prot->d2hring_hp2p_txcpl->inited = FALSE;
	}
	if (prot->d2hring_hp2p_rxcpl == NULL) {
		prot->d2hring_hp2p_rxcpl = MALLOCZ(prot->osh, sizeof(msgbuf_ring_t));

		if (prot->d2hring_hp2p_rxcpl == NULL) {
			DHD_ERROR(("%s: couldn't alloc memory for d2hring_hp2p_rxcpl\n",
				__FUNCTION__));
			return BCME_NOMEM;
		}

		/* create the hp2p rx completion ring next to hp2p tx compl ring
		* ringid = id next to hp2p tx compl ring
		*/
		ringid = ringid + 1;

		DHD_INFO(("%s: about to create hp2p rxcpl ring\n", __FUNCTION__));
		ret = dhd_prot_ring_attach(dhd, prot->d2hring_hp2p_rxcpl, "d2hhp2p_rxcpl",
			dhd_bus_get_hp2p_ring_max_size(dhd->bus, FALSE), D2HRING_RXCMPLT_ITEMSIZE,
			ringid);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: couldn't alloc resources for hp2p rxcpl ring\n",
				__FUNCTION__));
			goto err1;
		}
	} else {
		/* for re-entry case, clear inited flag */
		prot->d2hring_hp2p_rxcpl->inited = FALSE;
	}

	if (prot->d2hring_hp2p_rxcpl != NULL &&
		prot->d2hring_hp2p_txcpl != NULL) {
		/* dhd_prot_init rentry after a dhd_prot_reset */
		ret = BCME_OK;
	}

	return ret;
err1:
	MFREE(prot->osh, prot->d2hring_hp2p_rxcpl, sizeof(msgbuf_ring_t));
	prot->d2hring_hp2p_rxcpl = NULL;

err2:
	MFREE(prot->osh, prot->d2hring_hp2p_txcpl, sizeof(msgbuf_ring_t));
	prot->d2hring_hp2p_txcpl = NULL;
	return ret;
} /* dhd_check_create_hp2p_rings */

int
dhd_prot_init_hp2p_rings(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = BCME_OK;

	dhd->hp2p_ring_more = TRUE;
	/* default multiflow not allowed */
	dhd->hp2p_mf_enable = FALSE;

	if ((ret = dhd_check_create_hp2p_rings(dhd)) != BCME_OK) {
		DHD_ERROR(("%s: hp2p rings aren't created! \n",
			__FUNCTION__));
		return ret;
	}

	if ((prot->d2hring_hp2p_txcpl->inited) || (prot->d2hring_hp2p_txcpl->create_pending)) {
		DHD_INFO(("hp2p tx completion ring was created!\n"));
		return ret;
	}

	DHD_TRACE(("trying to send create d2h hp2p txcpl ring: id %d\n",
		prot->d2hring_hp2p_txcpl->idx));
	ret = dhd_send_d2h_ringcreate(dhd, prot->d2hring_hp2p_txcpl,
		BCMPCIE_D2H_RING_TYPE_HPP_TX_CPL, DHD_D2H_HPPRING_TXREQ_PKTID);
	if (ret != BCME_OK)
		return ret;

	prot->d2hring_hp2p_txcpl->seqnum = D2H_EPOCH_INIT_VAL;
	prot->d2hring_hp2p_txcpl->current_phase = BCMPCIE_CMNHDR_PHASE_BIT_INIT;

	if ((prot->d2hring_hp2p_rxcpl->inited) || (prot->d2hring_hp2p_rxcpl->create_pending)) {
		DHD_INFO(("hp2p rx completion ring was created!\n"));
		return ret;
	}

	DHD_TRACE(("trying to send create d2h hp2p rxcpl ring: id %d\n",
		prot->d2hring_hp2p_rxcpl->idx));
	ret = dhd_send_d2h_ringcreate(dhd, prot->d2hring_hp2p_rxcpl,
		BCMPCIE_D2H_RING_TYPE_HPP_RX_CPL, DHD_D2H_HPPRING_RXREQ_PKTID);
	if (ret != BCME_OK)
		return ret;

	prot->d2hring_hp2p_rxcpl->seqnum = D2H_EPOCH_INIT_VAL;
	prot->d2hring_hp2p_rxcpl->current_phase = BCMPCIE_CMNHDR_PHASE_BIT_INIT;

	/* Note that there is no way to delete d2h or h2d ring deletion incase either fails,
	 * so can not cleanup if one ring was created while the other failed
	 */
	return BCME_OK;
} /* dhd_prot_init_hp2p_rings */

static void
dhd_prot_detach_hp2p_rings(dhd_pub_t *dhd)
{
	if (dhd->prot->d2hring_hp2p_txcpl) {
		dhd_prot_ring_detach(dhd, dhd->prot->d2hring_hp2p_txcpl);
		MFREE(dhd->prot->osh, dhd->prot->d2hring_hp2p_txcpl, sizeof(msgbuf_ring_t));
		dhd->prot->d2hring_hp2p_txcpl = NULL;
	}
	if (dhd->prot->d2hring_hp2p_rxcpl) {
		dhd_prot_ring_detach(dhd, dhd->prot->d2hring_hp2p_rxcpl);
		MFREE(dhd->prot->osh, dhd->prot->d2hring_hp2p_rxcpl, sizeof(msgbuf_ring_t));
		dhd->prot->d2hring_hp2p_rxcpl = NULL;
	}
}
#endif /* DHD_HP2P */

#ifdef BTLOG
static int
dhd_check_create_btlog_rings(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = BCME_ERROR;
	uint16 ringid;

	if (dhd->submit_count_WAR) {
		ringid = dhd->bus->max_tx_flowrings + BCMPCIE_COMMON_MSGRINGS + 2;
	} else {
		/* ringid is one less than ringids assign by dhd_check_create_info_rings */
		ringid = dhd->bus->max_tx_flowrings +
			(dhd->bus->max_submission_rings - dhd->bus->max_tx_flowrings) +
			BCMPCIE_H2D_COMMON_MSGRINGS - 1;
	}

	if (prot->d2hring_btlog_cpln) {
		/* for re-entry case, clear inited flag */
		prot->d2hring_btlog_cpln->inited = FALSE;
	}

	if (prot->h2dring_btlog_subn && prot->d2hring_btlog_cpln) {
		return BCME_OK; /* dhd_prot_init rentry after a dhd_prot_reset */
	}

	if (prot->h2dring_btlog_subn == NULL) {
		prot->h2dring_btlog_subn = MALLOCZ(prot->osh, sizeof(msgbuf_ring_t));

		if (prot->h2dring_btlog_subn == NULL) {
			DHD_ERROR(("%s: couldn't alloc memory for h2dring_btlog_subn\n",
				__FUNCTION__));
			return BCME_NOMEM;
		}

		DHD_INFO(("%s: about to create debug submit ring\n", __FUNCTION__));
		ret = dhd_prot_ring_attach(dhd, prot->h2dring_btlog_subn, "h2dbtlog",
			H2DRING_DYNAMIC_INFO_MAX_ITEM, H2DRING_INFO_BUFPOST_ITEMSIZE,
			ringid);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: couldn't alloc resources for dbg submit ring\n",
				__FUNCTION__));
			goto err;
		}
	}

	if (prot->d2hring_btlog_cpln == NULL) {
		prot->d2hring_btlog_cpln = MALLOCZ(prot->osh, sizeof(msgbuf_ring_t));

		if (prot->d2hring_btlog_cpln == NULL) {
			DHD_ERROR(("%s: couldn't alloc memory for h2dring_btlog_subn\n",
				__FUNCTION__));
			return BCME_NOMEM;
		}

		if (dhd->submit_count_WAR) {
			ringid = ringid + 1;
		} else {
			/* advance ringid past BTLOG submit ring and INFO submit and cmplt rings */
			ringid = ringid + 3;
		}

		DHD_INFO(("%s: about to create debug cpl ring\n", __FUNCTION__));
		ret = dhd_prot_ring_attach(dhd, prot->d2hring_btlog_cpln, "d2hbtlog",
			D2HRING_DYNAMIC_INFO_MAX_ITEM, D2HRING_INFO_BUFCMPLT_ITEMSIZE,
			ringid);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: couldn't alloc resources for dbg cpl ring\n",
				__FUNCTION__));
			dhd_prot_ring_detach(dhd, prot->h2dring_btlog_subn);
			goto err;
		}
	}

	return ret;
err:
	MFREE(prot->osh, prot->h2dring_btlog_subn, sizeof(msgbuf_ring_t));

	if (prot->d2hring_btlog_cpln) {
		MFREE(prot->osh, prot->d2hring_btlog_cpln, sizeof(msgbuf_ring_t));
	}
	return ret;
} /* dhd_check_create_btlog_rings */

int
dhd_prot_init_btlog_rings(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = BCME_OK;

	if ((ret = dhd_check_create_btlog_rings(dhd)) != BCME_OK) {
		DHD_ERROR(("%s: btlog rings aren't created! \n",
			__FUNCTION__));
		return ret;
	}

	if ((prot->d2hring_btlog_cpln->inited) || (prot->d2hring_btlog_cpln->create_pending)) {
		DHD_INFO(("Info completion ring was created!\n"));
		return ret;
	}

	DHD_ERROR(("trying to send create d2h btlog ring: id %d\n", prot->d2hring_btlog_cpln->idx));
	ret = dhd_send_d2h_ringcreate(dhd, prot->d2hring_btlog_cpln,
		BCMPCIE_D2H_RING_TYPE_BTLOG_CPL, DHD_D2H_BTLOGRING_REQ_PKTID);
	if (ret != BCME_OK)
		return ret;

	prot->h2dring_btlog_subn->seqnum = H2D_EPOCH_INIT_VAL;
	prot->h2dring_btlog_subn->current_phase = 0;
	prot->d2hring_btlog_cpln->seqnum = D2H_EPOCH_INIT_VAL;
	prot->d2hring_btlog_cpln->current_phase = BCMPCIE_CMNHDR_PHASE_BIT_INIT;

	DHD_ERROR(("trying to send create h2d btlog ring id %d\n", prot->h2dring_btlog_subn->idx));
	prot->h2dring_btlog_subn->n_completion_ids = 1;
	prot->h2dring_btlog_subn->compeltion_ring_ids[0] = prot->d2hring_btlog_cpln->idx;

	ret = dhd_send_h2d_ringcreate(dhd, prot->h2dring_btlog_subn,
		BCMPCIE_H2D_RING_TYPE_BTLOG_SUBMIT, DHD_H2D_BTLOGRING_REQ_PKTID);

	/* Note that there is no way to delete d2h or h2d ring deletion incase either fails,
	 * so can not cleanup if one ring was created while the other failed
	 */
	return ret;
} /* dhd_prot_init_btlog_rings */

static void
dhd_prot_detach_btlog_rings(dhd_pub_t *dhd)
{
	if (dhd->prot->h2dring_btlog_subn) {
		dhd_prot_ring_detach(dhd, dhd->prot->h2dring_btlog_subn);
		MFREE(dhd->prot->osh, dhd->prot->h2dring_btlog_subn, sizeof(msgbuf_ring_t));
	}
	if (dhd->prot->d2hring_btlog_cpln) {
		dhd_prot_ring_detach(dhd, dhd->prot->d2hring_btlog_cpln);
		MFREE(dhd->prot->osh, dhd->prot->d2hring_btlog_cpln, sizeof(msgbuf_ring_t));
	}
}
#endif	/* BTLOG */

#ifdef EWP_EDL
static int
dhd_check_create_edl_rings(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = BCME_ERROR;
	uint16 ringid;

#ifdef BTLOG
	if (dhd->submit_count_WAR) {
		ringid = dhd->bus->max_tx_flowrings + BCMPCIE_COMMON_MSGRINGS;
	} else
#endif	/* BTLOG */
	{
		/* dongle may increase max_submission_rings so keep
		 * ringid at end of dynamic rings (re-use info ring cpl ring id)
		 */
		ringid = dhd->bus->max_tx_flowrings +
			(dhd->bus->max_submission_rings - dhd->bus->max_tx_flowrings) +
			BCMPCIE_H2D_COMMON_MSGRINGS + 1;
	}

	if (prot->d2hring_edl) {
		prot->d2hring_edl->inited = FALSE;
		return BCME_OK; /* dhd_prot_init rentry after a dhd_prot_reset */
	}

	if (prot->d2hring_edl == NULL) {
		prot->d2hring_edl = MALLOCZ(prot->osh, sizeof(msgbuf_ring_t));

		if (prot->d2hring_edl == NULL) {
			DHD_ERROR(("%s: couldn't alloc memory for d2hring_edl\n",
				__FUNCTION__));
			return BCME_NOMEM;
		}

		DHD_ERROR(("%s: about to create EDL ring, ringid: %u \n", __FUNCTION__,
			ringid));
		ret = dhd_prot_ring_attach(dhd, prot->d2hring_edl, "d2hring_edl",
			D2HRING_EDL_MAX_ITEM, D2HRING_EDL_ITEMSIZE,
			ringid);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: couldn't alloc resources for EDL ring\n",
				__FUNCTION__));
			goto err;
		}
	}

	return ret;
err:
	MFREE(prot->osh, prot->d2hring_edl, sizeof(msgbuf_ring_t));
	prot->d2hring_edl = NULL;

	return ret;
} /* dhd_check_create_btlog_rings */

int
dhd_prot_init_edl_rings(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = BCME_ERROR;

	if ((ret = dhd_check_create_edl_rings(dhd)) != BCME_OK) {
		DHD_ERROR(("%s: EDL rings aren't created! \n",
			__FUNCTION__));
		return ret;
	}

	if ((prot->d2hring_edl->inited) || (prot->d2hring_edl->create_pending)) {
		DHD_INFO(("EDL completion ring was created!\n"));
		return ret;
	}

	DHD_ERROR(("trying to send create d2h edl ring: idx %d\n", prot->d2hring_edl->idx));
	ret = dhd_send_d2h_ringcreate(dhd, prot->d2hring_edl,
		BCMPCIE_D2H_RING_TYPE_EDL, DHD_D2H_DBGRING_REQ_PKTID);
	if (ret != BCME_OK)
		return ret;

	prot->d2hring_edl->seqnum = D2H_EPOCH_INIT_VAL;
	prot->d2hring_edl->current_phase = BCMPCIE_CMNHDR_PHASE_BIT_INIT;

	return BCME_OK;
} /* dhd_prot_init_btlog_rings */

static void
dhd_prot_detach_edl_rings(dhd_pub_t *dhd)
{
	if (dhd->prot->d2hring_edl) {
		dhd_prot_ring_detach(dhd, dhd->prot->d2hring_edl);
		MFREE(dhd->prot->osh, dhd->prot->d2hring_edl, sizeof(msgbuf_ring_t));
		dhd->prot->d2hring_edl = NULL;
	}
}
#endif	/* EWP_EDL */

/**
 * Initialize protocol: sync w/dongle state.
 * Sets dongle media info (iswl, drv_version, mac address).
 */
int dhd_sync_with_dongle(dhd_pub_t *dhd)
{
	int ret = 0;
	uint len = 0;
	wlc_rev_info_t revinfo;
	char buf[128];
	dhd_prot_t *prot = dhd->prot;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_os_set_ioctl_resp_timeout(IOCTL_RESP_TIMEOUT);

	/* Post ts buffer after shim layer is attached */
	ret = dhd_msgbuf_rxbuf_post_ts_bufs(dhd);

	/* query for 'wlc_ver' to get version info from firmware */
	/* memsetting to zero */
	bzero(buf, sizeof(buf));
	len = bcm_mkiovar("wlc_ver", NULL, 0, buf, sizeof(buf));
	if (len == 0) {
		DHD_ERROR(("%s failed in calling bcm_mkiovar %u\n", __FUNCTION__, len));
		ret = BCME_ERROR;
		goto done;
	}
	ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf), FALSE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	} else {
		dhd->wlc_ver_major = ((wl_wlc_version_t*)buf)->wlc_ver_major;
		dhd->wlc_ver_minor = ((wl_wlc_version_t*)buf)->wlc_ver_minor;
	}

	DHD_ERROR(("wlc_ver_major %d, wlc_ver_minor %d\n", dhd->wlc_ver_major, dhd->wlc_ver_minor));
#ifndef OEM_ANDROID
	/* Get the device MAC address */
	bzero(buf, sizeof(buf));
	strlcpy(buf, "cur_etheraddr", sizeof(buf));
	ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf), FALSE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s: GET iovar cur_etheraddr FAILED\n", __FUNCTION__));
		goto done;
	}
	memcpy(dhd->mac.octet, buf, ETHER_ADDR_LEN);
	if (dhd_msg_level & DHD_INFO_VAL) {
		bcm_print_bytes("CUR_ETHERADDR ", (uchar *)buf, ETHER_ADDR_LEN);
	}
#endif /* OEM_ANDROID */

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

	/* Get the RxBuf post size */
	/* Use default value in case of failure */
	prot->rxbufpost_sz = DHD_FLOWRING_RX_BUFPOST_PKTSZ;
	memset(buf, 0, sizeof(buf));
	len = bcm_mkiovar("rxbufpost_sz", NULL, 0, buf, sizeof(buf));
	if (len == 0) {
		DHD_ERROR(("%s failed in calling bcm_mkiovar %u\n", __FUNCTION__, len));
	} else {
		ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf), FALSE, 0);
		if (ret < 0) {
			DHD_ERROR(("%s: GET RxBuf post FAILED, use default %d\n",
				__FUNCTION__, DHD_FLOWRING_RX_BUFPOST_PKTSZ));
		} else {
			if (memcpy_s(&(prot->rxbufpost_sz), sizeof(prot->rxbufpost_sz),
					buf, sizeof(uint16)) != BCME_OK) {
				DHD_ERROR(("%s: rxbufpost_sz memcpy failed\n", __FUNCTION__));
			}

			if (prot->rxbufpost_sz > DHD_FLOWRING_RX_BUFPOST_PKTSZ_MAX) {
				DHD_ERROR(("%s: Invalid RxBuf post size : %d, default to %d\n",
					__FUNCTION__, prot->rxbufpost_sz,
					DHD_FLOWRING_RX_BUFPOST_PKTSZ));
					prot->rxbufpost_sz = DHD_FLOWRING_RX_BUFPOST_PKTSZ;
			} else {
				DHD_ERROR(("%s: RxBuf Post : %d\n",
					__FUNCTION__, prot->rxbufpost_sz));
			}
		}
	}

	/* Post buffers for packet reception */
	dhd_msgbuf_rxbuf_post(dhd, FALSE); /* alloc pkt ids */

	DHD_SSSR_DUMP_INIT(dhd);

	dhd_process_cid_mac(dhd, TRUE);
	ret = dhd_preinit_ioctls(dhd);
	dhd_process_cid_mac(dhd, FALSE);
#if defined(DHD_SDTC_ETB_DUMP)
	dhd_sdtc_etb_init(dhd);
#endif /* DHD_SDTC_ETB_DUMP */
#if defined(DHD_H2D_LOG_TIME_SYNC)
#ifdef DHD_HP2P
	if (FW_SUPPORTED(dhd, h2dlogts) || dhd->hp2p_capable)
#else
	if (FW_SUPPORTED(dhd, h2dlogts))
#endif // endif
	{
#ifdef DHD_HP2P
		if (dhd->hp2p_enable) {
			dhd->dhd_rte_time_sync_ms = DHD_H2D_LOG_TIME_STAMP_MATCH / 40;
		} else {
			dhd->dhd_rte_time_sync_ms = DHD_H2D_LOG_TIME_STAMP_MATCH;
		}
#else
		dhd->dhd_rte_time_sync_ms = DHD_H2D_LOG_TIME_STAMP_MATCH;
#endif /* DHD_HP2P */
		dhd->bus->dhd_rte_time_sync_count = OSL_SYSUPTIME_US();
		/* This is during initialization. */
		dhd_h2d_log_time_sync(dhd);
	} else {
		dhd->dhd_rte_time_sync_ms = 0;
	}
#endif /* DHD_H2D_LOG_TIME_SYNC */

#ifdef HOST_SFH_LLC
	if (FW_SUPPORTED(dhd, host_sfhllc)) {
		dhd->host_sfhllc_supported = TRUE;
	} else {
		dhd->host_sfhllc_supported = FALSE;
	}
#endif /* HOST_SFH_LLC */

	/* Always assumes wl for now */
	dhd->iswl = TRUE;
done:
	return ret;
} /* dhd_sync_with_dongle */

#define DHD_DBG_SHOW_METADATA	0

#if DHD_DBG_SHOW_METADATA
static void
BCMFASTPATH(dhd_prot_print_metadata)(dhd_pub_t *dhd, void *ptr, int len)
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

static INLINE void
BCMFASTPATH(dhd_prot_packet_free)(dhd_pub_t *dhd, void *pkt, uint8 pkttype, bool send)
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

/**
 * dhd_prot_packet_get should be called only for items having pktid_ctrl_map handle
 * and all the bottom most functions like dhd_pktid_map_free hold separate DHD_PKTID_LOCK
 * to ensure thread safety, so no need to hold any locks for this function
 */
static INLINE void *
BCMFASTPATH(dhd_prot_packet_get)(dhd_pub_t *dhd, uint32 pktid, uint8 pkttype, bool free_pktid)
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

	return PKTBUF;
}

#ifdef IOCTLRESP_USE_CONSTMEM
static INLINE void
BCMFASTPATH(dhd_prot_ioctl_ret_buffer_get)(dhd_pub_t *dhd, uint32 pktid, dhd_dma_buf_t *retbuf)
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

static void
BCMFASTPATH(dhd_msgbuf_rxbuf_post)(dhd_pub_t *dhd, bool use_rsv_pktid)
{
	dhd_prot_t *prot = dhd->prot;
	int16 fillbufs;
	int retcount = 0;

	fillbufs = prot->max_rxbufpost - prot->rxbufpost;
	while (fillbufs >= RX_BUF_BURST) {
		/* Post in a burst of 32 buffers at a time */
		fillbufs = MIN(fillbufs, RX_BUF_BURST);

		/* Post buffers */
		retcount = dhd_prot_rxbuf_post(dhd, fillbufs, use_rsv_pktid);

		if (retcount > 0) {
			prot->rxbufpost += (uint16)retcount;
			/* how many more to post */
			fillbufs = prot->max_rxbufpost - prot->rxbufpost;
		} else {
			/* Make sure we don't run loop any further */
			fillbufs = 0;
		}
	}
}

/** Post 'count' no of rx buffers to dongle */
static int
BCMFASTPATH(dhd_prot_rxbuf_post)(dhd_pub_t *dhd, uint16 count, bool use_rsv_pktid)
{
	void *p, **pktbuf;
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
#ifdef BCM_ROUTER_DHD
	uint16 pktsz = DHD_FLOWRING_RX_BUFPOST_PKTSZ + BCMEXTRAHDROOM;
#else
	uint16 pktsz = prot->rxbufpost_sz;
#endif /* BCM_ROUTER_DHD */

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

#ifdef BCM_ROUTER_DHD
		/* Reserve extra headroom for router builds */
		PKTPULL(dhd->osh, p, BCMEXTRAHDROOM);
#endif /* BCM_ROUTER_DHD */
		pktlen[i] = PKTLEN(dhd->osh, p);
		pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen[i], DMA_RX, p, 0);

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

	/* grab the ring lock to allocate pktid and post on ring */
	DHD_RING_LOCK(ring->ring_lock, flags);

	/* Claim space for exactly 'count' no of messages, for mitigation purpose */
	msg_start = (void *)
		dhd_prot_alloc_ring_space(dhd, ring, count, &alloced, TRUE);
	if (msg_start == NULL) {
		DHD_INFO(("%s:%d: Rxbufpost Msgbuf Not available\n", __FUNCTION__, __LINE__));
		DHD_RING_UNLOCK(ring->ring_lock, flags);
		goto cleanup;
	}
	/* if msg_start !=  NULL, we should have alloced space for atleast 1 item */
	ASSERT(alloced > 0);

	rxbuf_post_tmp = (uint8*)msg_start;

	for (i = 0; i < alloced; i++) {
		rxbuf_post = (host_rxbuf_post_t *)rxbuf_post_tmp;
		p = pktbuf[i];
		pa = pktbuf_pa[i];

		pktid = DHD_NATIVE_TO_PKTID(dhd, dhd->prot->pktid_rx_map, p, pa,
			pktlen[i], DMA_RX, NULL, ring->dma_buf.secdma, PKTTYPE_DATA_RX);
#if defined(DHD_PCIE_PKTID)
		if (pktid == DHD_PKTID_INVALID) {
			break;
		}
#endif /* DHD_PCIE_PKTID */

#ifdef DHD_HMAPTEST
	if (dhd->prot->hmaptest_rx_active == HMAPTEST_D11_RX_ACTIVE) {
		/* scratchbuf area */
		dhd->prot->hmap_rx_buf_va = (char *)dhd->prot->hmaptest.mem.va
			+ dhd->prot->hmaptest.offset;

		dhd->prot->hmap_rx_buf_len = pktlen[i] + prot->rx_metadata_offset;
		if ((dhd->prot->hmap_rx_buf_va +  dhd->prot->hmap_rx_buf_len) >
			((char *)dhd->prot->hmaptest.mem.va + dhd->prot->hmaptest.mem.len)) {
			DHD_ERROR(("hmaptest: ERROR Rxpost outside HMAPTEST buffer\n"));
			DHD_ERROR(("hmaptest: NOT Replacing Rx Buffer\n"));
			dhd->prot->hmaptest_rx_active = HMAPTEST_D11_RX_INACTIVE;
			dhd->prot->hmaptest.in_progress = FALSE;
		} else {
			pa = DMA_MAP(dhd->osh, dhd->prot->hmap_rx_buf_va,
				dhd->prot->hmap_rx_buf_len, DMA_RX, p, 0);

			dhd->prot->hmap_rx_buf_pa = pa;
			dhd->prot->hmaptest_rx_pktid = pktid;
			dhd->prot->hmaptest_rx_active = HMAPTEST_D11_RX_POSTED;
			DHD_ERROR(("hmaptest: d11write rxpost scratch rxbuf pktid=0x%08x\n",
				pktid));
			DHD_ERROR(("hmaptest: d11write rxpost scratch rxbuf va=0x%p pa.lo=0x%08x\n",
				dhd->prot->hmap_rx_buf_va, (uint32)PHYSADDRLO(pa)));
			DHD_ERROR(("hmaptest: d11write rxpost orig pktdata va=0x%p pa.lo=0x%08x\n",
				PKTDATA(dhd->osh, p), (uint32)PHYSADDRLO(pktbuf_pa[i])));
		}
	}
#endif /* DHD_HMAPTEST */
		dhd->prot->tot_rxbufpost++;
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
#ifdef DHD_LBUF_AUDIT
		PKTAUDIT(dhd->osh, p);
#endif
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
		dhd_prot_ring_write_complete(dhd, ring, msg_start, alloced);
	}

	DHD_RING_UNLOCK(ring->ring_lock, flags);

cleanup:
	for (i = alloced; i < count; i++) {
		p = pktbuf[i];
		pa = pktbuf_pa[i];

		DMA_UNMAP(dhd->osh, pa, pktlen[i], DMA_RX, 0, DHD_DMAH_NULL);
		PKTFREE(dhd->osh, p, FALSE);
	}

	MFREE(dhd->osh, lcl_buf, lcl_buf_size);
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return alloced;
} /* dhd_prot_rxbufpost */

#if !defined(BCM_ROUTER_DHD)
static int
dhd_prot_infobufpost(dhd_pub_t *dhd, msgbuf_ring_t *ring)
{
	unsigned long flags;
	uint32 pktid;
	dhd_prot_t *prot = dhd->prot;
	uint16 alloced = 0;
	uint16 pktsz = DHD_INFOBUF_RX_BUFPOST_PKTSZ;
	uint32 pktlen;
	info_buf_post_msg_t *infobuf_post;
	uint8 *infobuf_post_tmp;
	void *p;
	void* msg_start;
	uint8 i = 0;
	dmaaddr_t pa;
	int16 count = 0;

	if (ring == NULL)
		return 0;

	if (ring->inited != TRUE)
		return 0;
	if (ring == dhd->prot->h2dring_info_subn) {
		if (prot->max_infobufpost == 0)
			return 0;

		count = prot->max_infobufpost - prot->infobufpost;
	}
#ifdef BTLOG
	else if (ring == dhd->prot->h2dring_btlog_subn) {
		if (prot->max_btlogbufpost == 0)
			return 0;

		pktsz = DHD_BTLOG_RX_BUFPOST_PKTSZ;
		count = prot->max_btlogbufpost - prot->btlogbufpost;
	}
#endif	/* BTLOG */
	else {
		DHD_ERROR(("Unknown ring\n"));
		return 0;
	}

	if (count <= 0) {
		DHD_INFO(("%s: Cannot post more than max info resp buffers\n",
			__FUNCTION__));
		return 0;
	}

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	/* grab the ring lock to allocate pktid and post on ring */
	DHD_RING_LOCK(ring->ring_lock, flags);

	/* Claim space for exactly 'count' no of messages, for mitigation purpose */
	msg_start = (void *) dhd_prot_alloc_ring_space(dhd, ring, count, &alloced, FALSE);

	if (msg_start == NULL) {
		DHD_INFO(("%s:%d: infobufpost Msgbuf Not available\n", __FUNCTION__, __LINE__));
		DHD_RING_UNLOCK(ring->ring_lock, flags);
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
		pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen, DMA_RX, p, 0);
		if (PHYSADDRISZERO(pa)) {
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

		pktid = DHD_NATIVE_TO_PKTID(dhd, dhd->prot->pktid_ctrl_map, p, pa,
			pktlen, DMA_RX, NULL, ring->dma_buf.secdma, PKTTYPE_INFO_RX);

#if defined(DHD_PCIE_PKTID)
		if (pktid == DHD_PKTID_INVALID) {
			DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, 0);

#ifdef DHD_USE_STATIC_CTRLBUF
			PKTFREE_STATIC(dhd->osh, p, FALSE);
#else
			PKTFREE(dhd->osh, p, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
			DHD_ERROR_RLMT(("%s: Pktid pool depleted.\n", __FUNCTION__));
			break;
		}
#endif /* DHD_PCIE_PKTID */

		infobuf_post->host_buf_len = htol16((uint16)pktlen);
		infobuf_post->host_buf_addr.high_addr = htol32(PHYSADDRHI(pa));
		infobuf_post->host_buf_addr.low_addr = htol32(PHYSADDRLO(pa));

#ifdef DHD_PKTID_AUDIT_RING
		DHD_PKTID_AUDIT(dhd, prot->pktid_ctrl_map, pktid, DHD_DUPLICATE_ALLOC);
#endif /* DHD_PKTID_AUDIT_RING */

		DHD_MSGBUF_INFO(("ID %d, low_addr 0x%08x, high_addr 0x%08x\n",
			infobuf_post->cmn_hdr.request_id,  infobuf_post->host_buf_addr.low_addr,
			infobuf_post->host_buf_addr.high_addr));

		infobuf_post->cmn_hdr.request_id = htol32(pktid);
		/* Move rxbuf_post_tmp to next item */
		infobuf_post_tmp = infobuf_post_tmp + ring->item_len;
#ifdef DHD_LBUF_AUDIT
		PKTAUDIT(dhd->osh, p);
#endif
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
		if (ring == dhd->prot->h2dring_info_subn) {
			prot->infobufpost += alloced;
		}
#ifdef BTLOG
		if (ring == dhd->prot->h2dring_btlog_subn) {
			prot->btlogbufpost += alloced;
		}
#endif	/* BTLOG */
		dhd_prot_ring_write_complete(dhd, ring, msg_start, alloced);
	}

	DHD_RING_UNLOCK(ring->ring_lock, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return alloced;
} /* dhd_prot_infobufpost */
#endif /* !BCM_ROUTER_DHD */

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
		/* XXX: may be add an assert */
		return -1;
	}
#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK) {
		 return BCME_ERROR;
	}
#endif /* PCIE_INB_DW */

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
			goto fail;
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
			goto fail;
		}

		pktlen = PKTLEN(dhd->osh, p);
		pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen, DMA_RX, p, 0);

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

	/* grab the ring lock to allocate pktid and post on ring */
	DHD_RING_LOCK(ring->ring_lock, flags);

	rxbuf_post = (ioctl_resp_evt_buf_post_msg_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (rxbuf_post == NULL) {
		DHD_RING_UNLOCK(ring->ring_lock, flags);
		DHD_ERROR(("%s:%d: Ctrl submit Msgbuf Not available to post buffer \n",
			__FUNCTION__, __LINE__));

#ifdef IOCTLRESP_USE_CONSTMEM
		if (non_ioctl_resp_buf)
#endif /* IOCTLRESP_USE_CONSTMEM */
		{
			DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, DHD_DMAH_NULL);
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
		DHD_RING_UNLOCK(ring->ring_lock, flags);
		DMA_UNMAP(dhd->osh, pa, pktlen, DMA_RX, 0, DHD_DMAH_NULL);
		DHD_ERROR_RLMT(("%s: Pktid pool depleted.\n", __FUNCTION__));
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
		DHD_RING_UNLOCK(ring->ring_lock, flags);
#ifdef IOCTLRESP_USE_CONSTMEM
		if (non_ioctl_resp_buf)
#endif /* IOCTLRESP_USE_CONSTMEM */
		{
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
#ifdef DHD_LBUF_AUDIT
	if (non_ioctl_resp_buf)
		PKTAUDIT(dhd->osh, p);
#endif
	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, rxbuf_post, 1);

	DHD_RING_UNLOCK(ring->ring_lock, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return 1;

free_pkt_return:
	if (!non_ioctl_resp_buf) {
#ifdef IOCTLRESP_USE_CONSTMEM
		free_ioctl_return_buffer(dhd, &retbuf);
#else
		dhd_prot_packet_free(dhd, p, buf_type, FALSE);
#endif /* IOCTLRESP_USE_CONSTMEM */
	} else {
		dhd_prot_packet_free(dhd, p, buf_type, FALSE);
	}

fail:
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

	DHD_MSGBUF_INFO(("max to post %d, event %d \n", max_to_post, msg_type));

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
	DHD_MSGBUF_INFO(("posted %d buffers of type %d\n", i, msg_type));
	return (uint16)i;
}

static void
dhd_msgbuf_rxbuf_post_ioctlresp_bufs(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int max_to_post;

	DHD_MSGBUF_INFO(("ioctl resp buf post\n"));
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

	max_to_post = prot->max_eventbufpost - prot->cur_event_bufs_posted;
	if (max_to_post <= 0) {
		DHD_ERROR(("%s: Cannot post more than max event buffers\n",
			__FUNCTION__));
		return;
	}
	prot->cur_event_bufs_posted += dhd_msgbuf_rxbuf_post_ctrlpath(dhd,
		MSG_TYPE_EVENT_BUF_POST, max_to_post);
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

bool
BCMFASTPATH(dhd_prot_process_msgbuf_infocpl)(dhd_pub_t *dhd, uint bound)
{
	dhd_prot_t *prot = dhd->prot;
	bool more = TRUE;
	uint n = 0;
	msgbuf_ring_t *ring = prot->d2hring_info_cpln;
	unsigned long flags;

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

		if (dhd->smmu_fault_occurred) {
			more = FALSE;
			break;
		}

		DHD_RING_LOCK(ring->ring_lock, flags);
		/* Get the message from ring */
		msg_addr = dhd_prot_get_read_addr(dhd, ring, &msg_len);
		DHD_RING_UNLOCK(ring->ring_lock, flags);
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

#ifdef BTLOG
bool
BCMFASTPATH(dhd_prot_process_msgbuf_btlogcpl)(dhd_pub_t *dhd, uint bound)
{
	dhd_prot_t *prot = dhd->prot;
	bool more = TRUE;
	uint n = 0;
	msgbuf_ring_t *ring = prot->d2hring_btlog_cpln;

	if (ring == NULL)
		return FALSE;
	if (ring->inited != TRUE)
		return FALSE;

	/* Process all the messages - DTOH direction */
	while (!dhd_is_device_removed(dhd)) {
		uint8 *msg_addr;
		uint32 msg_len;

		if (dhd_query_bus_erros(dhd)) {
			more = FALSE;
			break;
		}

		if (dhd->hang_was_sent) {
			more = FALSE;
			break;
		}

		if (dhd->smmu_fault_occurred) {
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
#endif	/* BTLOG */

#ifdef EWP_EDL
bool
dhd_prot_process_msgbuf_edl(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring = prot->d2hring_edl;
	unsigned long flags = 0;
	uint32 items = 0;
	uint16 rd = 0;
	uint16 depth = 0;

	if (ring == NULL)
		return FALSE;
	if (ring->inited != TRUE)
		return FALSE;
	if (ring->item_len == 0) {
		DHD_ERROR(("%s: Bad ring ! ringidx %d, item_len %d \n",
			__FUNCTION__, ring->idx, ring->item_len));
		return FALSE;
	}

	if (dhd_query_bus_erros(dhd)) {
		return FALSE;
	}

	if (dhd->hang_was_sent) {
		return FALSE;
	}

	/* in this DPC context just check if wr index has moved
	 * and schedule deferred context to actually process the
	 * work items.
	*/

	/* update the write index */
	DHD_RING_LOCK(ring->ring_lock, flags);
	if (dhd->dma_d2h_ring_upd_support) {
		/* DMAing write/read indices supported */
		ring->wr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
	} else {
		dhd_bus_cmn_readshared(dhd->bus, &ring->wr, RING_WR_UPD, ring->idx);
	}
	rd = ring->rd;
	DHD_RING_UNLOCK(ring->ring_lock, flags);

	depth = ring->max_items;
	/* check for avail space, in number of ring items */
	items = READ_AVAIL_SPACE(ring->wr, rd, depth);
	if (items == 0) {
		/* no work items in edl ring */
		return FALSE;
	}
	if (items > ring->max_items) {
		DHD_ERROR(("\r\n======================= \r\n"));
		DHD_ERROR(("%s(): ring %p, ring->name %s, ring->max_items %d, items %d \r\n",
			__FUNCTION__, ring, ring->name, ring->max_items, items));
		DHD_ERROR(("wr: %d,  rd: %d,  depth: %d  \r\n",
			ring->wr, ring->rd, depth));
		DHD_ERROR(("dhd->busstate %d bus->wait_for_d3_ack %d \r\n",
			dhd->busstate, dhd->bus->wait_for_d3_ack));
		DHD_ERROR(("\r\n======================= \r\n"));
#ifdef SUPPORT_LINKDOWN_RECOVERY
		if (ring->wr >= ring->max_items) {
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
		dhd_schedule_reset(dhd);

		return FALSE;
	}

	if (items > D2HRING_EDL_WATERMARK) {
		DHD_ERROR_RLMT(("%s: WARNING! EDL watermark hit, num items=%u;"
			" rd=%u; wr=%u; depth=%u;\n", __FUNCTION__, items,
			ring->rd, ring->wr, depth));
	}

	dhd_schedule_logtrace(dhd->info);

	return FALSE;
}

/*
 * This is called either from work queue context of 'event_log_dispatcher_work' or
 * from the kthread context of dhd_logtrace_thread
 */
int
dhd_prot_process_edl_complete(dhd_pub_t *dhd, void *evt_decode_data)
{
	dhd_prot_t *prot = NULL;
	msgbuf_ring_t *ring = NULL;
	int err = 0;
	unsigned long flags = 0;
	cmn_msg_hdr_t *msg = NULL;
	uint8 *msg_addr = NULL;
	uint32 max_items_to_process = 0, n = 0;
	uint32 num_items = 0, new_items = 0;
	uint16 depth = 0;
	volatile uint16 wr = 0;

	if (!dhd || !dhd->prot)
		return 0;

	prot = dhd->prot;
	ring = prot->d2hring_edl;

	if (!ring || !evt_decode_data) {
		return 0;
	}

	if (dhd->hang_was_sent) {
		return FALSE;
	}

	DHD_RING_LOCK(ring->ring_lock, flags);
	ring->curr_rd = ring->rd;
	wr = ring->wr;
	depth = ring->max_items;
	/* check for avail space, in number of ring items
	 * Note, that this will only give the # of items
	 * from rd to wr if wr>=rd, or from rd to ring end
	 * if wr < rd. So in the latter case strictly speaking
	 * not all the items are read. But this is OK, because
	 * these will be processed in the next doorbell as rd
	 * would have wrapped around. Processing in the next
	 * doorbell is acceptable since EDL only contains debug data
	 */
	num_items = READ_AVAIL_SPACE(wr, ring->rd, depth);

	if (num_items == 0) {
		/* no work items in edl ring */
		DHD_RING_UNLOCK(ring->ring_lock, flags);
		return 0;
	}

	DHD_INFO(("%s: EDL work items [%u] available \n",
			__FUNCTION__, num_items));

	/* if space is available, calculate address to be read */
	msg_addr = (char*)ring->dma_buf.va + (ring->rd * ring->item_len);

	max_items_to_process = MIN(num_items, DHD_EVENT_LOGTRACE_BOUND);

	DHD_RING_UNLOCK(ring->ring_lock, flags);

	/* Prefetch data to populate the cache */
	OSL_PREFETCH(msg_addr);

	n = max_items_to_process;
	while (n > 0) {
		msg = (cmn_msg_hdr_t *)msg_addr;
		/* wait for DMA of work item to complete */
		if ((err = dhd->prot->d2h_edl_sync_cb(dhd, ring, msg)) != BCME_OK) {
			DHD_ERROR(("%s: Error waiting for DMA to cmpl in EDL ring; err = %d\n",
				__FUNCTION__, err));
		}
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

		if (err != BCME_OK) {
			return 0;
		}

		/* process the edl work item, i.e, the event log */
		err = dhd_event_logtrace_process_edl(dhd, msg_addr, evt_decode_data);

		/* Dummy sleep so that scheduler kicks in after processing any logprints */
		OSL_SLEEP(0);

		/* Prefetch data to populate the cache */
		OSL_PREFETCH(msg_addr + ring->item_len);

		msg_addr += ring->item_len;
		--n;
	}

	DHD_RING_LOCK(ring->ring_lock, flags);
	/* update host ring read pointer */
	if ((ring->rd + max_items_to_process) >= ring->max_items)
		ring->rd = 0;
	else
		ring->rd += max_items_to_process;
	DHD_RING_UNLOCK(ring->ring_lock, flags);

	/* Now after processing max_items_to_process update dongle rd index.
	 * The TCM rd index is updated only if bus is not
	 * in D3. Else, the rd index is updated from resume
	 * context in - 'dhdpcie_bus_suspend'
	 */
	DHD_GENERAL_LOCK(dhd, flags);
	if (DHD_BUS_CHECK_SUSPEND_OR_ANY_SUSPEND_IN_PROGRESS(dhd)) {
		DHD_INFO(("%s: bus is in suspend(%d) or suspending(0x%x) state!!\n",
			__FUNCTION__, dhd->busstate, dhd->dhd_bus_busy_state));
		DHD_GENERAL_UNLOCK(dhd, flags);
	} else {
		DHD_GENERAL_UNLOCK(dhd, flags);
		DHD_EDL_RING_TCM_RD_UPDATE(dhd);
	}

	/* if num_items > bound, then anyway we will reschedule and
	 * this function runs again, so that if in between the DPC has
	 * updated the wr index, then the updated wr is read. But if
	 * num_items <= bound, and if DPC executes and updates the wr index
	 * when the above while loop is running, then the updated 'wr' index
	 * needs to be re-read from here, If we don't do so, then till
	 * the next time this function is scheduled
	 * the event logs will not be processed.
	*/
	if (num_items <= DHD_EVENT_LOGTRACE_BOUND) {
		/* read the updated wr index if reqd. and update num_items */
		DHD_RING_LOCK(ring->ring_lock, flags);
		if (wr != (volatile uint16)ring->wr) {
			wr = (volatile uint16)ring->wr;
			new_items = READ_AVAIL_SPACE(wr, ring->rd, depth);
			DHD_INFO(("%s: new items [%u] avail in edl\n",
				__FUNCTION__, new_items));
			num_items += new_items;
		}
		DHD_RING_UNLOCK(ring->ring_lock, flags);
	}

	/* if # of items processed is less than num_items, need to re-schedule
	* the deferred ctx
	*/
	if (max_items_to_process < num_items) {
		DHD_INFO(("%s: EDL bound hit / new items found, "
				"items processed=%u; remaining=%u, "
				"resched deferred ctx...\n",
				__FUNCTION__, max_items_to_process,
				num_items - max_items_to_process));
		return (num_items - max_items_to_process);
	}

	return 0;

}

void
dhd_prot_edl_ring_tcm_rd_update(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = NULL;
	unsigned long flags = 0;
	msgbuf_ring_t *ring = NULL;

	if (!dhd)
		return;

	prot = dhd->prot;
	if (!prot || !prot->d2hring_edl)
		return;

	ring = prot->d2hring_edl;
	DHD_RING_LOCK(ring->ring_lock, flags);
	dhd_prot_upd_read_idx(dhd, ring);
	DHD_RING_UNLOCK(ring->ring_lock, flags);
	if (dhd->dma_h2d_ring_upd_support &&
		!IDMA_ACTIVE(dhd)) {
		dhd_prot_ring_doorbell(dhd, DHD_RDPTR_UPDATE_H2D_DB_MAGIC(ring));
	}
}
#endif /* EWP_EDL */

static void
dhd_prot_rx_frame(dhd_pub_t *dhd, void *pkt, int ifidx, uint pkt_count)
{

#ifdef DHD_LB_RXP
	if (dhd_read_lb_rxp(dhd) == 1) {
		dhd_lb_rx_pkt_enqueue(dhd, pkt, ifidx);
		return;
	}
#endif /* DHD_LB_RXP */
	dhd_bus_rx_frame(dhd->bus, pkt, ifidx, pkt_count);
}

#ifdef DHD_LB_RXP
static int dhd_prot_lb_rxp_flow_ctrl(dhd_pub_t *dhd)
{
	if ((dhd->lb_rxp_stop_thr == 0) || (dhd->lb_rxp_strt_thr == 0)) {
		/* when either of stop and start thresholds are zero flow ctrl is not enabled */
		return FALSE;
	}

	if ((dhd_lb_rxp_process_qlen(dhd) >= dhd->lb_rxp_stop_thr) &&
			(!atomic_read(&dhd->lb_rxp_flow_ctrl))) {
		atomic_set(&dhd->lb_rxp_flow_ctrl, TRUE);
#ifdef DHD_LB_STATS
		dhd->lb_rxp_stop_thr_hitcnt++;
#endif /* DHD_LB_STATS */
		DHD_INFO(("lb_rxp_process_qlen %d lb_rxp_stop_thr %d\n",
			dhd_lb_rxp_process_qlen(dhd), dhd->lb_rxp_stop_thr));
	} else if ((dhd_lb_rxp_process_qlen(dhd) <= dhd->lb_rxp_strt_thr) &&
			(atomic_read(&dhd->lb_rxp_flow_ctrl))) {
		atomic_set(&dhd->lb_rxp_flow_ctrl, FALSE);
#ifdef DHD_LB_STATS
		dhd->lb_rxp_strt_thr_hitcnt++;
#endif /* DHD_LB_STATS */
		DHD_INFO(("lb_rxp_process_qlen %d lb_rxp_strt_thr %d\n",
			dhd_lb_rxp_process_qlen(dhd), dhd->lb_rxp_strt_thr));
	}

	return atomic_read(&dhd->lb_rxp_flow_ctrl);
}
#endif /* DHD_LB_RXP */

/** called when DHD needs to check for 'receive complete' messages from the dongle */
bool
BCMFASTPATH(dhd_prot_process_msgbuf_rxcpl)(dhd_pub_t *dhd, uint bound, int ringtype)
{
	bool more = FALSE;
	uint n = 0;
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring;
	uint16 item_len;
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

#ifdef DHD_LB_RXP
	/* must be the first check in this function */
	if (dhd_prot_lb_rxp_flow_ctrl(dhd)) {
		/* DHD is holding a lot of RX packets.
		 * Just give chance for netwrok stack to consumes RX packets.
		 */
		return FALSE;
	}
#endif /* DHD_LB_RXP */
#ifdef DHD_PCIE_RUNTIMEPM
	/* Set rx_pending_due_to_rpm if device is not in resume state */
	if (dhdpcie_runtime_bus_wake(dhd, FALSE, dhd_prot_process_msgbuf_rxcpl)) {
		dhd->rx_pending_due_to_rpm = TRUE;
		return more;
	}
	dhd->rx_pending_due_to_rpm = FALSE;
#endif /* DHD_PCIE_RUNTIMEPM */

#ifdef DHD_HP2P
	if (ringtype == DHD_HP2P_RING && prot->d2hring_hp2p_rxcpl)
		ring = prot->d2hring_hp2p_rxcpl;
	else
#endif /* DHD_HP2P */
		ring = &prot->d2hring_rx_cpln;
	item_len = ring->item_len;
	while (1) {
		if (dhd_is_device_removed(dhd))
			break;

		if (dhd_query_bus_erros(dhd))
			break;

		if (dhd->hang_was_sent)
			break;

		if (dhd->smmu_fault_occurred) {
			break;
		}

		pkt_cnt = 0;
		pktqhead = pkt_newidx = NULL;
		pkt_cnt_newidx = 0;

		DHD_RING_LOCK(ring->ring_lock, flags);

		/* Get the address of the next message to be read from ring */
		msg_addr = dhd_prot_get_read_addr(dhd, ring, &msg_len);
		if (msg_addr == NULL) {
			DHD_RING_UNLOCK(ring->ring_lock, flags);
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
			/* Sanity check of shinfo nrfrags */
			if (!pkt || (dhd_check_shinfo_nrfrags(dhd, pkt, &pa, pktid) != BCME_OK)) {
				msg_len -= item_len;
				msg_addr += item_len;
				continue;
			}
			dhd->prot->tot_rxcpl++;

			DMA_UNMAP(dhd->osh, pa, (uint) len, DMA_RX, 0, dmah);

#ifdef DMAMAP_STATS
			dhd->dma_stats.rxdata--;
			dhd->dma_stats.rxdata_sz -= len;
#endif /* DMAMAP_STATS */
#ifdef DHD_HMAPTEST
			if ((dhd->prot->hmaptest_rx_active == HMAPTEST_D11_RX_POSTED) &&
				(pktid == dhd->prot->hmaptest_rx_pktid)) {

				uchar *ptr;
				ptr = PKTDATA(dhd->osh, pkt) - (prot->rx_metadata_offset);
				DMA_UNMAP(dhd->osh, dhd->prot->hmap_rx_buf_pa,
					(uint)dhd->prot->hmap_rx_buf_len, DMA_RX, 0, dmah);
				DHD_ERROR(("hmaptest: d11write rxcpl rcvd sc rxbuf pktid=0x%08x\n",
					pktid));
				DHD_ERROR(("hmaptest: d11write rxcpl r0_st=0x%08x r1_stat=0x%08x\n",
					msg->rx_status_0, msg->rx_status_1));
				DHD_ERROR(("hmaptest: d11write rxcpl rxbuf va=0x%p pa=0x%08x\n",
					dhd->prot->hmap_rx_buf_va,
					(uint32)PHYSADDRLO(dhd->prot->hmap_rx_buf_pa)));
				DHD_ERROR(("hmaptest: d11write rxcpl pktdata va=0x%p pa=0x%08x\n",
					PKTDATA(dhd->osh, pkt), (uint32)PHYSADDRLO(pa)));
				memcpy(ptr, dhd->prot->hmap_rx_buf_va, dhd->prot->hmap_rx_buf_len);
				dhd->prot->hmaptest_rx_active = HMAPTEST_D11_RX_INACTIVE;
				dhd->prot->hmap_rx_buf_va = NULL;
				dhd->prot->hmap_rx_buf_len = 0;
				PHYSADDRHISET(dhd->prot->hmap_rx_buf_pa, 0);
				PHYSADDRLOSET(dhd->prot->hmap_rx_buf_pa, 0);
				prot->hmaptest.in_progress = FALSE;
			}
#endif /* DHD_HMAPTEST */
			DHD_MSGBUF_INFO(("id 0x%04x, offset %d, len %d, idx %d, phase 0x%02x, "
				"pktdata %p, metalen %d\n",
				ltoh32(msg->cmn_hdr.request_id),
				ltoh16(msg->data_offset),
				ltoh16(msg->data_len), msg->cmn_hdr.if_id,
				msg->cmn_hdr.flags, PKTDATA(dhd->osh, pkt),
				ltoh16(msg->metadata_len)));

			pkt_cnt++;
			msg_len -= item_len;
			msg_addr += item_len;

#if !defined(BCM_ROUTER_DHD)
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
#endif /* !BCM_ROUTER_DHD */

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
#ifdef DHD_PKTTS
			if (dhd_get_pktts_enab(dhd) == TRUE) {
				uint fwr1 = 0, fwr2 = 0;

				/* firmware mark rx_pktts.tref with 0xFFFFFFFF for errors */
				if (ltoh32(msg->rx_pktts.tref) != 0xFFFFFFFF) {
					fwr1 = (uint)htonl(ltoh32(msg->rx_pktts.tref));
					fwr2 = (uint)htonl(ltoh32(msg->rx_pktts.tref) +
						ltoh16(msg->rx_pktts.d_t2));

					/* check for overflow */
					if (ntohl(fwr2) > ntohl(fwr1)) {
						/* send rx timestamp to netlnik socket */
						dhd_msgbuf_send_msg_rx_ts(dhd, pkt, fwr1, fwr2);
					}
				}
			}
#endif /* DHD_PKTTS */

#if defined(WL_MONITOR)
			if (dhd_monitor_enabled(dhd, ifidx)) {
				if (msg->flags & BCMPCIE_PKT_FLAGS_FRAME_802_11) {
					dhd_rx_mon_pkt(dhd, msg, pkt, ifidx);
					continue;
				} else {
					DHD_ERROR(("Received non 802.11 packet, "
						"when monitor mode is enabled\n"));
				}
			}
#endif /* WL_MONITOR */

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

#ifdef DHD_HP2P
			if (dhd->hp2p_capable && ring == prot->d2hring_hp2p_rxcpl) {
#ifdef DHD_HP2P_DEBUG
				bcm_print_bytes("Rxcpl", (uchar *)msg,  sizeof(host_rxbuf_cmpl_t));
#endif /* DHD_HP2P_DEBUG */
				dhd_update_hp2p_rxstats(dhd, msg);
			}
#endif /* DHD_HP2P */

#ifdef DHD_TIMESYNC
			if (dhd->prot->rx_ts_log_enabled) {
				dhd_pkt_parse_t parse;
				ts_timestamp_t *ts = (ts_timestamp_t *)&msg->ts;

				memset(&parse, 0, sizeof(dhd_pkt_parse_t));
				dhd_parse_proto(PKTDATA(dhd->osh, pkt), &parse);

				if (parse.proto == IP_PROT_ICMP)
					dhd_timesync_log_rx_timestamp(dhd->ts, ifidx,
							ts->low, ts->high, &parse);
			}
#endif /* DHD_TIMESYNC */

#ifdef DHD_LBUF_AUDIT
			PKTAUDIT(dhd->osh, pkt);
#endif
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

		DHD_RING_UNLOCK(ring->ring_lock, flags);

		pkt = pktqhead;
		for (i = 0; pkt && i < pkt_cnt; i++, pkt = nextpkt) {
			nextpkt = PKTNEXT(dhd->osh, pkt);
			PKTSETNEXT(dhd->osh, pkt, NULL);
#ifdef DHD_RX_CHAINING
			dhd_rxchain_frame(dhd, pkt, ifidx);
#else
			dhd_prot_rx_frame(dhd, pkt, ifidx, 1);
#endif /* DHD_LB_RXP */
		}

		if (pkt_newidx) {
#ifdef DHD_RX_CHAINING
			dhd_rxchain_frame(dhd, pkt_newidx, if_newidx);
#else
			dhd_prot_rx_frame(dhd, pkt_newidx, if_newidx, 1);
#endif /* DHD_LB_RXP */
		}

		pkt_cnt += pkt_cnt_newidx;

		/* Post another set of rxbufs to the device */
		dhd_prot_return_rxbuf(dhd, ring, 0, pkt_cnt);

#ifdef DHD_RX_CHAINING
		dhd_rxchain_commit(dhd);
#endif

		/* After batch processing, check RX bound */
		n += pkt_cnt;
		if (n >= bound) {
			more = TRUE;
			break;
		}
	}

	/* Call lb_dispatch only if packets are queued */
	if (n &&
#ifdef WL_MONITOR
	!(dhd_monitor_enabled(dhd, ifidx)) &&
#endif /* WL_MONITOR */
	TRUE) {
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
bool
BCMFASTPATH(dhd_prot_process_msgbuf_txcpl)(dhd_pub_t *dhd, uint bound, int ringtype)
{
	bool more = TRUE;
	uint n = 0;
	msgbuf_ring_t *ring;
	unsigned long flags;

#ifdef DHD_HP2P
	if (ringtype == DHD_HP2P_RING && dhd->prot->d2hring_hp2p_txcpl)
		ring = dhd->prot->d2hring_hp2p_txcpl;
	else
#endif /* DHD_HP2P */
		ring = &dhd->prot->d2hring_tx_cpln;

	/* Process all the messages - DTOH direction */
	while (!dhd_is_device_removed(dhd)) {
		uint8 *msg_addr;
		uint32 msg_len;

		if (dhd_query_bus_erros(dhd)) {
			more = FALSE;
			break;
		}

		if (dhd->hang_was_sent) {
			more = FALSE;
			break;
		}

		if (dhd->smmu_fault_occurred) {
			more = FALSE;
			break;
		}

		DHD_RING_LOCK(ring->ring_lock, flags);
		/* Get the address of the next message to be read from ring */
		msg_addr = dhd_prot_get_read_addr(dhd, ring, &msg_len);
		DHD_RING_UNLOCK(ring->ring_lock, flags);

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

	if (n) {
		/* For IDMA and HWA case, doorbell is sent along with read index update.
		 * For DMA indices case ring doorbell once n items are read to sync with dongle.
		 */
		if (dhd->dma_h2d_ring_upd_support && !IDMA_ACTIVE(dhd)) {
			dhd_prot_ring_doorbell(dhd, DHD_RDPTR_UPDATE_H2D_DB_MAGIC(ring));
			dhd->prot->txcpl_db_cnt++;
		}
	}
	return more;
}

int
BCMFASTPATH(dhd_prot_process_trapbuf)(dhd_pub_t *dhd)
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
		if (dhd->db7_trap.fw_db7w_trap_inprogress) {
			DHD_ERROR(("DB7 FW responded 0x%04x\n", data));
		} else {
			DHD_ERROR(("Firmware trapped and trap_data is 0x%04x\n", data));
		}

		if (data & D2H_DEV_EXT_TRAP_DATA)
		{
			if (dhd->extended_trap_data) {
				OSL_CACHE_INV((void *)trap_addr->va,
				       BCMPCIE_EXT_TRAP_DATA_MAXLEN);
				memcpy(dhd->extended_trap_data, (uint32 *)trap_addr->va,
				       BCMPCIE_EXT_TRAP_DATA_MAXLEN);
			}
			if (dhd->db7_trap.fw_db7w_trap_inprogress == FALSE) {
				DHD_ERROR(("Extended trap data available\n"));
			}
		}
#ifdef BT_OVER_PCIE
		if (data & D2H_DEV_TRAP_DUE_TO_BT) {
			DHD_ERROR(("WLAN Firmware trapped due to BT\n"));
			dhd->dongle_trap_due_to_bt = TRUE;
		}
#endif /* BT_OVER_PCIE */
		return data;
	}
	return 0;
}

/** called when DHD needs to check for 'ioctl complete' messages from the dongle */
int
BCMFASTPATH(dhd_prot_process_ctrlbuf)(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring = &prot->d2hring_ctrl_cpln;
	unsigned long flags;

	/* Process all the messages - DTOH direction */
	while (!dhd_is_device_removed(dhd)) {
		uint8 *msg_addr;
		uint32 msg_len;

		if (dhd_query_bus_erros(dhd)) {
			break;
		}

		if (dhd->hang_was_sent) {
			break;
		}

		if (dhd->smmu_fault_occurred) {
			break;
		}

		DHD_RING_LOCK(ring->ring_lock, flags);
		/* Get the address of the next message to be read from ring */
		msg_addr = dhd_prot_get_read_addr(dhd, ring, &msg_len);
		DHD_RING_UNLOCK(ring->ring_lock, flags);

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
static int
BCMFASTPATH(dhd_prot_process_msgtype)(dhd_pub_t *dhd, msgbuf_ring_t *ring, uint8 *buf, uint32 len)
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

		if (dhd->smmu_fault_occurred) {
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

		DHD_MSGBUF_INFO(("msg_type %d item_len %d buf_len %d\n",
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

#if !defined(BCM_ROUTER_DHD)
		if (msg_type == MSG_TYPE_INFO_BUF_CMPLT) {
			if (ring == dhd->prot->d2hring_info_cpln) {
				if (!dhd->prot->infobufpost) {
					DHD_ERROR(("infobuf posted are zero,"
						   "but there is a completion\n"));
					goto done;
				}
				dhd->prot->infobufpost--;
				dhd_prot_infobufpost(dhd, dhd->prot->h2dring_info_subn);
				dhd_prot_process_infobuf_complete(dhd, buf);
			}
#ifdef BTLOG
			else if (ring == dhd->prot->d2hring_btlog_cpln) {
				info_buf_resp_t *resp = (info_buf_resp_t *)buf;

				if (!dhd->prot->btlogbufpost) {
					DHD_ERROR(("btlogbuf posted are zero,"
						   "but there is a completion\n"));
					goto done;
				}

				dhd->prot->btlogbufpost--;
				if (resp->compl_hdr.status != BCMPCIE_PKT_FLUSH) {
					dhd_prot_infobufpost(dhd, dhd->prot->h2dring_btlog_subn);
				}
				dhd_prot_process_btlog_complete(dhd, buf);
			}
#endif	/* BTLOG */
		} else
#endif	/* !defined(BCM_ROUTER_DHD) */
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
		/* XXX: if the request is ioctl request finish the ioctl, rather than timing out */
		DHD_ERROR(("Bad phase\n"));
	}
	if (status != BCMPCIE_BADOPTION)
		return;

	if (request_id == DHD_H2D_DBGRING_REQ_PKTID) {
		/* XXX: see if the debug ring create is pending */
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
		/* XXX: see if the debug ring create is pending */
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
#ifdef BTLOG
	else if (request_id == DHD_H2D_BTLOGRING_REQ_PKTID) {
		/* XXX: see if the debug ring create is pending */
		if (dhd->prot->h2dring_btlog_subn != NULL) {
			if (dhd->prot->h2dring_btlog_subn->create_pending == TRUE) {
				DHD_ERROR(("H2D ring create failed for btlog ring\n"));
				dhd->prot->h2dring_btlog_subn->create_pending = FALSE;
			}
			else
				DHD_ERROR(("ring create ID for a ring, create not pending\n"));
		} else {
			DHD_ERROR(("%s btlog submit ring doesn't exist\n", __FUNCTION__));
		}
	}
	else if (request_id == DHD_D2H_BTLOGRING_REQ_PKTID) {
		/* XXX: see if the debug ring create is pending */
		if (dhd->prot->d2hring_btlog_cpln != NULL) {
			if (dhd->prot->d2hring_btlog_cpln->create_pending == TRUE) {
				DHD_ERROR(("D2H ring create failed for btlog ring\n"));
				dhd->prot->d2hring_btlog_cpln->create_pending = FALSE;
			}
			else
				DHD_ERROR(("ring create ID for btlog ring, create not pending\n"));
		} else {
			DHD_ERROR(("%s btlog cpl ring doesn't exist\n", __FUNCTION__));
		}
	}
#endif	/* BTLOG */
#ifdef DHD_HP2P
	else if (request_id == DHD_D2H_HPPRING_TXREQ_PKTID) {
		/* XXX: see if the HPP txcmpl ring create is pending */
		if (dhd->prot->d2hring_hp2p_txcpl != NULL) {
			if (dhd->prot->d2hring_hp2p_txcpl->create_pending == TRUE) {
				DHD_ERROR(("H2D ring create failed for hp2p ring\n"));
				dhd->prot->d2hring_hp2p_txcpl->create_pending = FALSE;
			}
			else
				DHD_ERROR(("ring create ID for a ring, create not pending\n"));
		} else {
			DHD_ERROR(("%s hp2p txcmpl ring doesn't exist\n", __FUNCTION__));
		}
	}
	else if (request_id == DHD_D2H_HPPRING_RXREQ_PKTID) {
		/* XXX: see if the hp2p rxcmpl ring create is pending */
		if (dhd->prot->d2hring_hp2p_rxcpl != NULL) {
			if (dhd->prot->d2hring_hp2p_rxcpl->create_pending == TRUE) {
				DHD_ERROR(("D2H ring create failed for hp2p rxcmpl ring\n"));
				dhd->prot->d2hring_hp2p_rxcpl->create_pending = FALSE;
			}
			else
				DHD_ERROR(("ring create ID for hp2p rxcmpl ring, not pending\n"));
		} else {
			DHD_ERROR(("%s hp2p rxcpl ring doesn't exist\n", __FUNCTION__));
		}
	}
#endif /* DHD_HP2P */
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
#if defined(DHD_PKTID_AUDIT_RING) && !defined(BCM_ROUTER_DHD)
	uint32 pktid = ltoh32(ioct_ack->cmn_hdr.request_id);
#endif /* DHD_PKTID_AUDIT_RING && !BCM_ROUTER_DHD */

#if defined(DHD_PKTID_AUDIT_RING) && !defined(BCM_ROUTER_DHD)
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
#endif /* DHD_PKTID_AUDIT_RING && !BCM_ROUTER_DHD */

	dhd->prot->ioctl_ack_time = OSL_LOCALTIME_NS();

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
		/* FIXME: should we fail the pending IOCTL compelteion wait process... */
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

	/* Check for ioctl timeout induce flag, which is set by firing
	 * dhd iovar to induce IOCTL timeout. If flag is set,
	 * return from here, which results in to IOCTL timeout.
	 */
	if (dhd->dhd_induce_error == DHD_INDUCE_IOCTL_TIMEOUT) {
		DHD_ERROR(("%s: Inducing resumed on timeout\n", __FUNCTION__));
		return;
	}

	memset(&retbuf, 0, sizeof(dhd_dma_buf_t));

	pkt_id = ltoh32(ioct_resp->cmn_hdr.request_id);

#if defined(DHD_PKTID_AUDIT_RING) && !defined(BCM_ROUTER_DHD)
#ifndef IOCTLRESP_USE_CONSTMEM
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, prot->pktid_ctrl_map, pkt_id,
		DHD_DUPLICATE_FREE, msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
#else
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, prot->pktid_map_handle_ioctl, pkt_id,
		DHD_DUPLICATE_FREE, msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
#endif /* !IOCTLRESP_USE_CONSTMEM */
#endif /* DHD_PKTID_AUDIT_RING && !BCM_ROUTER_DHD */

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

	dhd->prot->ioctl_cmplt_time = OSL_LOCALTIME_NS();

	/* Clear Response pending bit */
	prot->ioctl_state &= ~MSGBUF_IOCTL_RESP_PENDING;
	DHD_GENERAL_UNLOCK(dhd, flags);

#ifndef IOCTLRESP_USE_CONSTMEM
	pkt = dhd_prot_packet_get(dhd, pkt_id, PKTTYPE_IOCTL_RX, TRUE);
#else
	dhd_prot_ioctl_ret_buffer_get(dhd, pkt_id, &retbuf);
	pkt = retbuf.va;
#endif /* !IOCTLRESP_USE_CONSTMEM */
	if (!pkt) {
		DHD_ERROR(("%s: received ioctl response with NULL pkt\n", __FUNCTION__));
		prhex("dhd_prot_ioctcmplt_process:",
			(uchar *)msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
		return;
	}

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

int
dhd_prot_check_tx_resource(dhd_pub_t *dhd)
{
	return dhd->prot->no_tx_resource;
}

#ifdef DHD_PKTTS
/**
 * dhd_msgbuf_get_ip_info - this api finds following (ipv4 and ipv6 are supported)
 * 1. pointer to data portion of pkt
 * 2. five tuple checksum of pkt
 *   = {scr_ip, dst_ip, src_port, dst_port, proto}
 * 3. ip_prec
 *
 * @dhdp: pointer to dhd_pub object
 * @pkt: packet pointer
 * @ptr: retuns pointer to data portion of pkt
 * @chksum: returns five tuple checksum of pkt
 * @prec: returns ip precedence
 * @tcp_seqno: returns tcp sequnce number
 *
 * returns packet length remaining after tcp/udp header or BCME_ERROR.
 */
static int
dhd_msgbuf_get_ip_info(dhd_pub_t *dhdp, void *pkt, void **ptr, uint32 *chksum,
	uint32 *prec, uint32 *tcp_seqno, uint32 *tcp_ackno)
{
	char *pdata;
	uint plen;
	uint32 type, len;
	uint32 checksum = 0;
	uint8 dscp_prio = 0;
	struct bcmtcp_hdr *tcp = NULL;

	pdata = PKTDATA(dhdp->osh, pkt);
	plen = PKTLEN(dhdp->osh, pkt);

	/* Ethernet header */
	if (plen < ETHER_HDR_LEN) {
		return BCME_ERROR;
	}
	type = ntoh16(((struct ether_header *)pdata)->ether_type);
	pdata += ETHER_HDR_LEN;
	plen -= ETHER_HDR_LEN;

	if ((type == ETHER_TYPE_IP) ||
		(type == ETHER_TYPE_IPV6)) {
		dscp_prio = (IP_TOS46(pdata) >> IPV4_TOS_PREC_SHIFT);
	}

	/* IP header (v4 or v6) */
	if (type == ETHER_TYPE_IP) {
		struct ipv4_hdr *iph = (struct ipv4_hdr *)pdata;
		if (plen <= sizeof(*iph)) {
			return BCME_ERROR;
		}

		len = IPV4_HLEN(iph);
		if (plen <= len || IP_VER(iph) != IP_VER_4 || len < IPV4_MIN_HEADER_LEN) {
			return BCME_ERROR;
		}

		type = IPV4_PROT(iph);
		pdata += len;
		plen -= len;

		checksum ^= bcm_compute_xor32((volatile uint32 *)iph->src_ip,
			sizeof(iph->src_ip) / sizeof(uint32));
		checksum ^= bcm_compute_xor32((volatile uint32 *)iph->dst_ip,
			sizeof(iph->dst_ip) / sizeof(uint32));
	} else if (type == ETHER_TYPE_IPV6) {
		struct ipv6_hdr *ip6h = (struct ipv6_hdr *)pdata;

		if (plen <= IPV6_MIN_HLEN || IP_VER(ip6h) != IP_VER_6) {
			return BCME_ERROR;
		}

		type = IPV6_PROT(ip6h);
		pdata += IPV6_MIN_HLEN;
		plen -= IPV6_MIN_HLEN;
		if (IPV6_EXTHDR(type)) {
			uint8 proto = 0;
			int32 exth_len = ipv6_exthdr_len(pdata, &proto);
			if (exth_len < 0 || ((plen -= exth_len) <= 0)) {
				return BCME_ERROR;
			}
			type = proto;
			pdata += exth_len;
			plen -= exth_len;
		}

		checksum ^= bcm_compute_xor32((volatile uint32 *)&ip6h->saddr,
			sizeof(ip6h->saddr) / sizeof(uint32));
		checksum ^= bcm_compute_xor32((volatile uint32 *)&ip6h->daddr,
			sizeof(ip6h->saddr) / sizeof(uint32));
	}

	/* return error if not TCP or UDP */
	if ((type != IP_PROT_UDP) && (type != IP_PROT_TCP)) {
		return BCME_ERROR;
	}

	/* src_port and dst_port (together 32bit) */
	checksum ^= bcm_compute_xor32((volatile uint32 *)pdata, 1);
	checksum ^= bcm_compute_xor32((volatile uint32 *)&type, 1);

	if (type == IP_PROT_TCP) {
		tcp = (struct bcmtcp_hdr *)pdata;
		len = TCP_HDRLEN(pdata[TCP_HLEN_OFFSET]) << 2;
	} else { /* IP_PROT_UDP */
		len =	sizeof(struct bcmudp_hdr);
	}

	/* length check */
	if (plen < len) {
		return BCME_ERROR;
	}

	pdata += len;
	plen -= len;

	/* update data[0] */
	*ptr = (void *)pdata;

	/* update fivetuple checksum */
	*chksum = checksum;

	/* update ip prec */
	*prec = dscp_prio;

	/* update tcp sequence number */
	if (tcp != NULL) {
		*tcp_seqno = tcp->seq_num;
		*tcp_ackno = tcp->ack_num;
	}

	return plen;
}

/**
 * dhd_msgbuf_send_msg_tx_ts - send pktts tx timestamp to netlnik socket
 *
 * @dhdp: pointer to dhd_pub object
 * @pkt: packet pointer
 * @fwts: firmware timestamp {fwt1..fwt4}
 * @version: pktlat version supported in firmware
 */
static void
dhd_msgbuf_send_msg_tx_ts(dhd_pub_t *dhdp, void *pkt, void *fw_ts, uint16 version)
{
	bcm_to_info_tx_ts_t to_tx_info;
	void *ptr = NULL;
	int dlen = 0;
	uint32 checksum = 0;
	uint32 prec = 0;
	pktts_flow_t *flow = NULL;
	uint32 flow_pkt_offset = 0;
	uint32 num_config = 0;
	uint32 tcp_seqno = 0;
	uint32 tcp_ackno = 0;

	dlen = dhd_msgbuf_get_ip_info(dhdp, pkt, &ptr, &checksum, &prec, &tcp_seqno, &tcp_ackno);

	flow = dhd_match_pktts_flow(dhdp, checksum, NULL, &num_config);
	if (flow) {
		/* there is valid config for this chksum */
		flow_pkt_offset = flow->pkt_offset;
	} else if (num_config) {
		/* there is valid config + no matching config for this chksum */
		return;
	} else {
		/* there is no valid config. pass all to netlink */
	}

	memset(&to_tx_info, 0, sizeof(to_tx_info));
	to_tx_info.hdr.type = BCM_TS_TX;
	to_tx_info.hdr.flowid = checksum;
	to_tx_info.hdr.prec = prec;

	/* special case: if flow is not configured, copy tcp seqno and ackno in xbytes */
	if (!flow && tcp_seqno) {
		uint32 *xbytes = (uint32 *)to_tx_info.hdr.xbytes;

		(void)memcpy_s(&xbytes[0], sizeof(xbytes[0]),
			((uint8 *)&tcp_seqno), sizeof(tcp_seqno));
		(void)memcpy_s(&xbytes[1], sizeof(xbytes[1]),
			((uint8 *)&tcp_ackno), sizeof(tcp_ackno));
	} else if ((dlen > flow_pkt_offset) &&
		((dlen - flow_pkt_offset) >= sizeof(to_tx_info.hdr.xbytes))) {
		(void)memcpy_s(to_tx_info.hdr.xbytes, sizeof(to_tx_info.hdr.xbytes),
			((uint8 *)ptr + flow_pkt_offset), sizeof(to_tx_info.hdr.xbytes));
	}

	to_tx_info.dhdt0 = DHD_PKT_GET_QTIME(pkt);
	to_tx_info.dhdt5 = OSL_SYSUPTIME_US();

	if (version == METADATA_VER_1) {
		struct pktts_fwtx_v1 *fwts = (struct pktts_fwtx_v1 *)fw_ts;

		to_tx_info.hdr.magic = BCM_TS_MAGIC;

		to_tx_info.fwts[0] = ntohl(fwts->ts[0]);
		to_tx_info.fwts[1] = ntohl(fwts->ts[1]);
		to_tx_info.fwts[2] = ntohl(fwts->ts[2]);
		to_tx_info.fwts[3] = ntohl(fwts->ts[3]);

		dhd_send_msg_to_ts(NULL, (void *)&to_tx_info, OFFSETOF(bcm_to_info_tx_ts_t, ucts));
	} else if (version == METADATA_VER_2) {
		struct pktts_fwtx_v2 *fwts = (struct pktts_fwtx_v2 *)fw_ts;

		to_tx_info.hdr.magic = BCM_TS_MAGIC_V2;

		to_tx_info.fwts[0] = ntohl(fwts->ts[0]);
		to_tx_info.fwts[1] = ntohl(fwts->ts[1]);
		to_tx_info.fwts[2] = ntohl(fwts->ts[2]);
		to_tx_info.fwts[3] = ntohl(fwts->ts[3]);

		to_tx_info.ucts[0] = ntohl(fwts->ut[0]);
		to_tx_info.ucts[1] = ntohl(fwts->ut[1]);
		to_tx_info.ucts[2] = ntohl(fwts->ut[2]);
		to_tx_info.ucts[3] = ntohl(fwts->ut[3]);
		to_tx_info.ucts[4] = ntohl(fwts->ut[4]);

		to_tx_info.uccnt[0] = ntohl(fwts->uc[0]);
		to_tx_info.uccnt[1] = ntohl(fwts->uc[1]);
		to_tx_info.uccnt[2] = ntohl(fwts->uc[2]);
		to_tx_info.uccnt[3] = ntohl(fwts->uc[3]);
		to_tx_info.uccnt[4] = ntohl(fwts->uc[4]);
		to_tx_info.uccnt[5] = ntohl(fwts->uc[5]);
		to_tx_info.uccnt[6] = ntohl(fwts->uc[6]);
		to_tx_info.uccnt[7] = ntohl(fwts->uc[7]);

		dhd_send_msg_to_ts(NULL, (void *)&to_tx_info, sizeof(to_tx_info));
	}
	return;
}

/**
 * dhd_msgbuf_send_msg_dx_ts - send pktts rx timestamp to netlnik socket
 *
 * @dhdp: pointer to dhd_pub object
 * @pkt: packet pointer
 * @fwr1: firmware timestamp at probe point 1
 * @fwr2: firmware timestamp at probe point 2
 */
static void
dhd_msgbuf_send_msg_rx_ts(dhd_pub_t *dhdp, void *pkt, uint fwr1, uint fwr2)
{
	bcm_to_info_rx_ts_t to_rx_info;
	void *ptr = NULL;
	int dlen = 0;
	uint32 checksum = 0;
	uint32 prec = 0;
	pktts_flow_t *flow = NULL;
	uint32 flow_pkt_offset = 0;
	uint32 num_config = 0;
	uint32 tcp_seqno = 0;
	uint32 tcp_ackno = 0;

	dlen = dhd_msgbuf_get_ip_info(dhdp, pkt, &ptr, &checksum, &prec, &tcp_seqno, &tcp_ackno);

	flow = dhd_match_pktts_flow(dhdp, checksum, NULL, &num_config);
	if (flow) {
		/* there is valid config for this chksum */
		flow_pkt_offset = flow->pkt_offset;
	} else if (num_config) {
		/* there is valid config + no matching config for this chksum */
		return;
	} else {
		/* there is no valid config. pass all to netlink */
	}

	memset(&to_rx_info, 0, sizeof(to_rx_info));
	to_rx_info.hdr.magic = BCM_TS_MAGIC;
	to_rx_info.hdr.type = BCM_TS_RX;
	to_rx_info.hdr.flowid = checksum;
	to_rx_info.hdr.prec = prec;

	/* special case: if flow is not configured, copy tcp seqno and ackno in xbytes */
	if (!flow && tcp_seqno) {
		uint32 *xbytes = (uint32 *)to_rx_info.hdr.xbytes;

		(void)memcpy_s(&xbytes[0], sizeof(xbytes[0]),
			((uint8 *)&tcp_seqno), sizeof(tcp_seqno));
		(void)memcpy_s(&xbytes[1], sizeof(xbytes[1]),
			((uint8 *)&tcp_ackno), sizeof(tcp_ackno));
	} else if ((dlen > flow_pkt_offset) &&
		((dlen - flow_pkt_offset) >= sizeof(to_rx_info.hdr.xbytes))) {
		(void)memcpy_s(to_rx_info.hdr.xbytes, sizeof(to_rx_info.hdr.xbytes),
			((uint8 *)ptr + flow_pkt_offset), sizeof(to_rx_info.hdr.xbytes));
	}

	to_rx_info.dhdr3 = OSL_SYSUPTIME_US();

	to_rx_info.fwts[0] = ntohl(fwr1);
	to_rx_info.fwts[1] = ntohl(fwr2);

	dhd_send_msg_to_ts(NULL, (void *)&to_rx_info, sizeof(to_rx_info));
	return;
}
#endif /* DHD_PKTTS */

/** called on MSG_TYPE_TX_STATUS message received from dongle */
static void
BCMFASTPATH(dhd_prot_txstatus_process)(dhd_pub_t *dhd, void *msg)
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
	msgbuf_ring_t *ring = &dhd->prot->d2hring_tx_cpln;
#if defined(TX_STATUS_LATENCY_STATS)
	flow_info_t *flow_info;
	uint64 tx_status_latency;
#endif /* TX_STATUS_LATENCY_STATS */
#ifdef AGG_H2D_DB
	msgbuf_ring_t *flow_ring;
#endif /* AGG_H2D_DB */
#if defined(DHD_AWDL) && defined(AWDL_SLOT_STATS)
	dhd_awdl_stats_t *awdl_stats;
	if_flow_lkup_t *if_flow_lkup;
	unsigned long awdl_stats_lock_flags;
	uint8 ifindex;
	uint8 role;
#endif /* DHD_AWDL && AWDL_SLOT_STATS */
	flow_ring_node_t *flow_ring_node;
	uint16 flowid;
#ifdef DHD_PKTTS
	struct metadata_txcmpl_v1 meta_ts_v1;
	struct metadata_txcmpl_v2 meta_ts_v2;
	dhd_dma_buf_t meta_data_buf;
	uint64 addr = 0;

	BCM_REFERENCE(meta_ts_v1);
	BCM_REFERENCE(meta_ts_v2);
	BCM_REFERENCE(meta_data_buf);
	BCM_REFERENCE(addr);

	if ((dhd->memdump_type == DUMP_TYPE_PKTID_AUDIT_FAILURE) ||
		(dhd->memdump_type == DUMP_TYPE_PKTID_INVALID)) {
		DHD_ERROR_RLMT(("%s: return as invalid pktid detected\n", __FUNCTION__));
		return;
	}

	memset(&meta_ts_v1, 0, sizeof(meta_ts_v1));
	memset(&meta_ts_v2, 0, sizeof(meta_ts_v2));
	memset(&meta_data_buf, 0, sizeof(meta_data_buf));
#endif /* DHD_PKTTS */
	txstatus = (host_txbuf_cmpl_t *)msg;

	flowid = txstatus->compl_hdr.flow_ring_id;
	flow_ring_node = DHD_FLOW_RING(dhd, flowid);
#ifdef AGG_H2D_DB
	flow_ring = DHD_RING_IN_FLOWRINGS_POOL(prot, flowid);
	OSL_ATOMIC_DEC(dhd->osh, &flow_ring->inflight);
#endif /* AGG_H2D_DB */

	BCM_REFERENCE(flow_ring_node);

#ifdef DEVICE_TX_STUCK_DETECT
	/**
	 * Since we got a completion message on this flowid,
	 * update tx_cmpl time stamp
	 */
	flow_ring_node->tx_cmpl = OSL_SYSUPTIME();
	/* update host copy of rd pointer */
#ifdef DHD_HP2P
	if (dhd->prot->d2hring_hp2p_txcpl &&
		flow_ring_node->flow_info.tid == HP2P_PRIO) {
		ring = dhd->prot->d2hring_hp2p_txcpl;
	}
#endif /* DHD_HP2P */
	ring->curr_rd++;
	if (ring->curr_rd >= ring->max_items) {
		ring->curr_rd = 0;
	}
#endif /* DEVICE_TX_STUCK_DETECT */

	/* locks required to protect circular buffer accesses */
	DHD_RING_LOCK(ring->ring_lock, flags);
	pktid = ltoh32(txstatus->cmn_hdr.request_id);

	if (dhd->pcie_txs_metadata_enable > 1) {
		/* Return metadata format (little endian):
		 * |<--- txstatus --->|<- metadatalen ->|
		 * |____|____|________|________|________|
		 * |    |    |        |        |> total delay from fetch to report (8-bit 1 = 4ms)
		 * |    |    |        |> ucode delay from enqueue to completion (8-bit 1 = 4ms)
		 * |    |    |> 8-bit reserved (pre-filled with original TX status by caller)
		 * |    |> delay time first fetch to the last fetch (4-bit 1 = 32ms)
		 * |> fetch count (4-bit)
		 */
		printf("TX status[%d] = %04x-%04x -> status = %d (%d/%dms + %d/%dms)\n", pktid,
			ltoh16(txstatus->tx_status_ext), ltoh16(txstatus->tx_status),
			(txstatus->tx_status & WLFC_CTL_PKTFLAG_MASK),
			((txstatus->tx_status >> 12) & 0xf),
			((txstatus->tx_status >> 8) & 0xf) * 32,
			((txstatus->tx_status_ext & 0xff) * 4),
			((txstatus->tx_status_ext >> 8) & 0xff) * 4);
	}
	pkt_fate = TRUE;

#if defined(DHD_PKTID_AUDIT_RING) && !defined(BCM_ROUTER_DHD)
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_tx_map, pktid,
			DHD_DUPLICATE_FREE, msg, D2HRING_TXCMPLT_ITEMSIZE);
#endif /* DHD_PKTID_AUDIT_RING && !BCM_ROUTER_DHD */

	DHD_MSGBUF_INFO(("txstatus for pktid 0x%04x\n", pktid));
	if (OSL_ATOMIC_DEC_RETURN(dhd->osh, &prot->active_tx_count) < 0) {
		DHD_ERROR(("Extra packets are freed\n"));
	}
	ASSERT(pktid != 0);

#ifdef DHD_HMAPTEST

	if ((dhd->prot->hmaptest_tx_active == HMAPTEST_D11_TX_POSTED) &&
		(pktid == dhd->prot->hmaptest_tx_pktid)) {
		DHD_ERROR(("hmaptest: d11read txcpl received sc txbuf pktid=0x%08x\n", pktid));
		DHD_ERROR(("hmaptest: d11read txcpl txstatus=0x%08x\n", txstatus->tx_status));
		DHD_ERROR(("hmaptest: d11read txcpl sc txbuf va=0x%p pa=0x%08x\n",
			dhd->prot->hmap_tx_buf_va, (uint32)PHYSADDRLO(dhd->prot->hmap_tx_buf_pa)));
		dhd->prot->hmaptest_tx_active = HMAPTEST_D11_TX_INACTIVE;
		dhd->prot->hmap_tx_buf_va = NULL;
		dhd->prot->hmap_tx_buf_len = 0;
		PHYSADDRHISET(dhd->prot->hmap_tx_buf_pa, 0);
		PHYSADDRLOSET(dhd->prot->hmap_tx_buf_pa, 0);
		prot->hmaptest.in_progress = FALSE;
	}
	/* original skb is kept as it is because its going to be freed  later in this path */
#endif /* DHD_HMAPTEST */

#ifdef DHD_PKTTS
	if (dhd_get_pktts_enab(dhd) &&
		dhd->pkt_metadata_buflen) {
		/* Handle the Metadata first */
		meta_data_buf.va = DHD_PKTID_RETREIVE_METADATA(dhd, dhd->prot->pktid_tx_map,
			meta_data_buf.pa, meta_data_buf._alloced, meta_data_buf.dmah, pktid);
		if (meta_data_buf.va) {
			if (dhd->pkt_metadata_version == METADATA_VER_1) {
				memcpy(&meta_ts_v1, meta_data_buf.va, sizeof(meta_ts_v1));
			} else if (dhd->pkt_metadata_version == METADATA_VER_2) {
				memcpy(&meta_ts_v2, meta_data_buf.va, sizeof(meta_ts_v2));
			}
			memcpy(&addr, &meta_data_buf.pa, sizeof(meta_data_buf.pa));
			DHD_TRACE(("%s(): pktid %d retrieved mdata buffer %p "
				"pa: %llx dmah: %p\r\n",  __FUNCTION__,
				pktid, meta_data_buf.va, addr,
				meta_data_buf.dmah));
		}
	}
#endif /* DHD_PKTTS */

	pkt = DHD_PKTID_TO_NATIVE(dhd, dhd->prot->pktid_tx_map, pktid,
		pa, len, dmah, secdma, PKTTYPE_DATA_TX);
	if (!pkt) {
		DHD_RING_UNLOCK(ring->ring_lock, flags);
#ifdef DHD_PKTTS
		/*
		 * Call the free function after the Ring Lock is released.
		 * This is becuase pcie_free_consistent is not supposed to be
		 * called with Interrupts Disabled
		 */
		if (meta_data_buf.va) {
			DMA_FREE_CONSISTENT(dhd->osh, meta_data_buf.va, meta_data_buf._alloced,
				meta_data_buf.pa, meta_data_buf.dmah);
		}
#endif /* DHD_PKTTS */
		DHD_ERROR(("%s: received txstatus with NULL pkt\n", __FUNCTION__));
		prhex("dhd_prot_txstatus_process:", (uchar *)msg, D2HRING_TXCMPLT_ITEMSIZE);
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

	if (DHD_PKTID_AVAIL(dhd->prot->pktid_tx_map) == DHD_PKTID_MIN_AVAIL_COUNT) {
		DHD_ERROR_RLMT(("%s: start tx queue as min pktids are available\n",
			__FUNCTION__));
		prot->pktid_txq_stop_cnt--;
		dhd->prot->no_tx_resource = FALSE;
		dhd_bus_start_queue(dhd->bus);
	}

	DMA_UNMAP(dhd->osh, pa, (uint) len, DMA_RX, 0, dmah);

#ifdef TX_STATUS_LATENCY_STATS
	/* update the tx status latency for flowid */
	flow_info = &flow_ring_node->flow_info;
	tx_status_latency = OSL_SYSUPTIME_US() - DHD_PKT_GET_QTIME(pkt);
#if defined(DHD_AWDL) && defined(AWDL_SLOT_STATS)
	if (dhd->pkt_latency > 0 &&
		tx_status_latency > (dhd->pkt_latency)) {
		DHD_ERROR(("Latency: %llu > %u aw_cnt: %u \n",
			tx_status_latency, dhd->pkt_latency,
			dhd->awdl_aw_counter));
	}
#endif /*  defined(DHD_AWDL) && defined(AWDL_SLOT_STATS) */
	flow_info->cum_tx_status_latency += tx_status_latency;
	flow_info->num_tx_status++;
#endif /* TX_STATUS_LATENCY_STATS */
#if defined(DHD_AWDL) && defined(AWDL_SLOT_STATS)
	/* update the tx status latency when this AWDL slot is active */
	if_flow_lkup = (if_flow_lkup_t *)dhd->if_flow_lkup;
	ifindex = flow_ring_node->flow_info.ifindex;
	role = if_flow_lkup[ifindex].role;
	if (role == WLC_E_IF_ROLE_AWDL) {
		awdl_stats = &dhd->awdl_stats[dhd->awdl_tx_status_slot];
		DHD_AWDL_STATS_LOCK(dhd->awdl_stats_lock, awdl_stats_lock_flags);
		awdl_stats->cum_tx_status_latency += tx_status_latency;
		awdl_stats->num_tx_status++;
		DHD_AWDL_STATS_UNLOCK(dhd->awdl_stats_lock, awdl_stats_lock_flags);
	}
#endif /* DHD_AWDL && AWDL_SLOT_STATS */

#ifdef HOST_SFH_LLC
	if (dhd->host_sfhllc_supported) {
		struct ether_header eth;
		if (!memcpy_s(&eth, sizeof(eth),
			PKTDATA(dhd->osh, pkt), sizeof(eth))) {
			if (dhd_8023_llc_to_ether_hdr(dhd->osh,
				&eth, pkt) != BCME_OK) {
				DHD_ERROR_RLMT(("%s: host sfh llc"
					" converstion to ether failed\n",
					__FUNCTION__));
			}
		}
	}
#endif /* HOST_SFH_LLC */

#ifdef DMAMAP_STATS
	dhd->dma_stats.txdata--;
	dhd->dma_stats.txdata_sz -= len;
#endif /* DMAMAP_STATS */
	pkt_fate = dhd_dbg_process_tx_status(dhd, pkt, pktid,
		ltoh16(txstatus->compl_hdr.status) & WLFC_CTL_PKTFLAG_MASK);
#ifdef DHD_PKT_LOGGING
	if (dhd->d11_tx_status) {
		uint16 status = ltoh16(txstatus->compl_hdr.status) &
			WLFC_CTL_PKTFLAG_MASK;
		dhd_handle_pktdata(dhd, ltoh32(txstatus->cmn_hdr.if_id),
			pkt, (uint8 *)PKTDATA(dhd->osh, pkt), pktid, len,
			&status, NULL, TRUE, FALSE, TRUE);
	}
#endif /* DHD_PKT_LOGGING */
#if defined(BCMPCIE) && (defined(LINUX) || defined(OEM_ANDROID) || defined(DHD_EFI))
	dhd_txcomplete(dhd, pkt, pkt_fate);
#ifdef DHD_4WAYM4_FAIL_DISCONNECT
	dhd_eap_txcomplete(dhd, pkt, pkt_fate, txstatus->cmn_hdr.if_id);
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */
#endif /* BCMPCIE && (defined(LINUX) || defined(OEM_ANDROID)) */

#ifdef DHD_PKTTS
	if (dhd_get_pktts_enab(dhd) == TRUE) {
		if (dhd->pkt_metadata_buflen) {
			/* firmware mark tx_pktts.tref with 0xFFFFFFFF for errors */
			if ((dhd->pkt_metadata_version == METADATA_VER_1) &&
					(ltoh32(meta_ts_v1.tref) != 0xFFFFFFFF)) {
				struct pktts_fwtx_v1 fwts;
				fwts.ts[0] = (uint32)htonl(ltoh32(meta_ts_v1.tref));
				fwts.ts[1] = (uint32)htonl(ltoh32(meta_ts_v1.tref) +
					ltoh16(meta_ts_v1.d_t2));
				fwts.ts[2] = (uint32)htonl(ltoh32(meta_ts_v1.tref) +
					ltoh16(meta_ts_v1.d_t3));
				fwts.ts[3] = (uint32)htonl(ltoh32(meta_ts_v1.tref) +
					ltoh16(meta_ts_v1.d_t4));
				/* check for overflow */
				if (ntohl(fwts.ts[3]) > ntohl(fwts.ts[0])) {
					/* send tx timestamp to netlink socket */
					dhd_msgbuf_send_msg_tx_ts(dhd, pkt, &fwts,
						dhd->pkt_metadata_version);
				}
			} else if ((dhd->pkt_metadata_version == METADATA_VER_2) &&
					(ltoh32(meta_ts_v2.tref) != 0xFFFFFFFF)) {
				struct pktts_fwtx_v2 fwts;
				fwts.ts[0] = (uint32)htonl(ltoh32(meta_ts_v2.tref));
				fwts.ts[1] = (uint32)htonl(ltoh32(meta_ts_v2.tref) +
					ltoh16(meta_ts_v2.d_t2));
				fwts.ts[2] = (uint32)htonl(ltoh32(meta_ts_v2.tref) +
					ltoh16(meta_ts_v2.d_t3));
				fwts.ts[3] = (uint32)htonl(ltoh32(meta_ts_v2.tref) +
					ltoh16(meta_ts_v2.d_t4));

				fwts.ut[0] = (uint32)htonl(ltoh32(meta_ts_v2.tref) +
					ltoh16(meta_ts_v2.u_t1));
				fwts.ut[1] = (uint32)htonl(ltoh16(meta_ts_v2.u_t2));
				fwts.ut[2] = (uint32)htonl(ltoh16(meta_ts_v2.u_t3));
				fwts.ut[3] = (uint32)htonl(ltoh16(meta_ts_v2.u_t4));
				fwts.ut[4] = (uint32)htonl(ltoh32(meta_ts_v2.tref) +
					ltoh16(meta_ts_v2.u_t5));

				fwts.uc[0] = (uint32)htonl(ltoh32(meta_ts_v2.u_c1));
				fwts.uc[1] = (uint32)htonl(ltoh32(meta_ts_v2.u_c2));
				fwts.uc[2] = (uint32)htonl(ltoh32(meta_ts_v2.u_c3));
				fwts.uc[3] = (uint32)htonl(ltoh32(meta_ts_v2.u_c4));
				fwts.uc[4] = (uint32)htonl(ltoh32(meta_ts_v2.u_c5));
				fwts.uc[5] = (uint32)htonl(ltoh32(meta_ts_v2.u_c6));
				fwts.uc[6] = (uint32)htonl(ltoh32(meta_ts_v2.u_c7));
				fwts.uc[7] = (uint32)htonl(ltoh32(meta_ts_v2.u_c8));

				DHD_INFO(("uct1:%x uct2:%x uct3:%x uct4:%x uct5:%x\n",
					ntohl(fwts.ut[0]), ntohl(fwts.ut[1]), ntohl(fwts.ut[2]),
					ntohl(fwts.ut[3]), ntohl(fwts.ut[4])));
				DHD_INFO(("ucc1:%x ucc2:%x ucc3:%x ucc4:%x"
					" ucc5:%x ucc6:%x ucc7:%x ucc8:%x\n",
					ntohl(fwts.uc[0]), ntohl(fwts.uc[1]), ntohl(fwts.uc[2]),
					ntohl(fwts.uc[3]), ntohl(fwts.uc[4]), ntohl(fwts.uc[5]),
					ntohl(fwts.uc[6]), ntohl(fwts.uc[7])));
				/* check for overflow */
				if (ntohl(fwts.ts[3]) > ntohl(fwts.ts[0])) {
					/* send tx timestamp to netlink socket */
					dhd_msgbuf_send_msg_tx_ts(dhd, pkt, &fwts,
						dhd->pkt_metadata_version);
				}
			}
		} else {
			/* firmware mark tx_pktts.tref with 0xFFFFFFFF for errors */
			if (ltoh32(txstatus->tx_pktts.tref) != 0xFFFFFFFF) {
				struct pktts_fwtx_v1 fwts;

				fwts.ts[0] = (uint32)htonl(ltoh32(txstatus->tx_pktts.tref));
				fwts.ts[1] = (uint32)htonl(ltoh32(txstatus->tx_pktts.tref) +
					ltoh16(txstatus->tx_pktts.d_t2));
				fwts.ts[2] = (uint32)htonl(ltoh32(txstatus->tx_pktts.tref) +
					ltoh16(txstatus->tx_pktts.d_t3));
				fwts.ts[3] = (uint32)htonl(ltoh32(txstatus->tx_pktts.tref) +
					ltoh16(txstatus->compl_hdr.tx_pktts.d_t4));

				/* check for overflow */
				if (ntohl(fwts.ts[3]) > ntohl(fwts.ts[0])) {
					/* send tx timestamp to netlnik socket */
					dhd_msgbuf_send_msg_tx_ts(dhd, pkt, &fwts, METADATA_VER_1);
				}
			}
		}
	}
#endif /* DHD_PKTTS */

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

#ifdef DHD_HP2P
	if (dhd->hp2p_capable && flow_ring_node->flow_info.tid == HP2P_PRIO) {
#ifdef DHD_HP2P_DEBUG
		bcm_print_bytes("txcpl", (uint8 *)txstatus, sizeof(host_txbuf_cmpl_t));
#endif /* DHD_HP2P_DEBUG */
		dhd_update_hp2p_txstats(dhd, txstatus);
	}
#endif /* DHD_HP2P */

#ifdef DHD_TIMESYNC
	if (dhd->prot->tx_ts_log_enabled) {
		dhd_pkt_parse_t parse;
		ts_timestamp_t *ts = (ts_timestamp_t *)&(txstatus->ts);

		memset(&parse, 0, sizeof(parse));
		dhd_parse_proto(PKTDATA(dhd->osh, pkt), &parse);

		if (parse.proto == IP_PROT_ICMP)
			dhd_timesync_log_tx_timestamp(dhd->ts,
				txstatus->compl_hdr.flow_ring_id,
				txstatus->cmn_hdr.if_id,
				ts->low, ts->high, &parse);
	}
#endif /* DHD_TIMESYNC */

#ifdef DHD_LBUF_AUDIT
	PKTAUDIT(dhd->osh, pkt);
#endif
	DHD_FLOWRING_TXSTATUS_CNT_UPDATE(dhd->bus, txstatus->compl_hdr.flow_ring_id,
		txstatus->tx_status);
	DHD_RING_UNLOCK(ring->ring_lock, flags);
#ifdef DHD_PKTTS
	if (meta_data_buf.va) {
		DMA_FREE_CONSISTENT(dhd->osh, meta_data_buf.va, meta_data_buf._alloced,
			meta_data_buf.pa, meta_data_buf.dmah);
	}
#endif /* DHD_PKTTS */
#ifdef DHD_MEM_STATS
	DHD_MEM_STATS_LOCK(dhd->mem_stats_lock, flags);
	DHD_MSGBUF_INFO(("%s txpath_mem: %llu PKTLEN: %d\n",
		__FUNCTION__, dhd->txpath_mem, PKTLEN(dhd->osh, pkt)));
	dhd->txpath_mem -= PKTLEN(dhd->osh, pkt);
	DHD_MEM_STATS_UNLOCK(dhd->mem_stats_lock, flags);
#endif /* DHD_MEM_STATS */
	PKTFREE(dhd->osh, pkt, TRUE);

	return;
} /* dhd_prot_txstatus_process */

/* FIXME: assuming that it is getting inline data related to the event data */
/** called on MSG_TYPE_WL_EVENT message received from dongle */
static void
dhd_prot_event_process(dhd_pub_t *dhd, void *msg)
{
	wlevent_req_msg_t *evnt;
	uint32 bufid;
	uint16 buflen;
	int ifidx = 0;
	void* pkt;
	dhd_prot_t *prot = dhd->prot;

	/* Event complete header */
	evnt = (wlevent_req_msg_t *)msg;
	bufid = ltoh32(evnt->cmn_hdr.request_id);

#if defined(DHD_PKTID_AUDIT_RING) && !defined(BCM_ROUTER_DHD)
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_ctrl_map, bufid,
			DHD_DUPLICATE_FREE, msg, D2HRING_CTRL_CMPLT_ITEMSIZE);
#endif /* DHD_PKTID_AUDIT_RING && !BCM_ROUTER_DHD */

	buflen = ltoh16(evnt->event_data_len);

	ifidx = BCMMSGBUF_API_IFIDX(&evnt->cmn_hdr);
	/* FIXME: check the event status */

	/* Post another rxbuf to the device */
	if (prot->cur_event_bufs_posted)
		prot->cur_event_bufs_posted--;
	dhd_msgbuf_rxbuf_post_event_bufs(dhd);

	pkt = dhd_prot_packet_get(dhd, bufid, PKTTYPE_EVENT_RX, TRUE);

	if (!pkt) {
		DHD_ERROR(("%s: pkt is NULL for pktid %d\n", __FUNCTION__, bufid));
		return;
	}

#if !defined(BCM_ROUTER_DHD)
	/* FIXME: make sure the length is more than dataoffset */
	/* DMA RX offset updated through shared area */
	if (dhd->prot->rx_dataoffset)
		PKTPULL(dhd->osh, pkt, dhd->prot->rx_dataoffset);
#endif /* !BCM_ROUTER_DHD */

	PKTSETLEN(dhd->osh, pkt, buflen);
#ifdef DHD_LBUF_AUDIT
	PKTAUDIT(dhd->osh, pkt);
#endif
	dhd_bus_rx_frame(dhd->bus, pkt, ifidx, 1);
}

#if !defined(BCM_ROUTER_DHD)
/** called on MSG_TYPE_INFO_BUF_CMPLT message received from dongle */
static void
BCMFASTPATH(dhd_prot_process_infobuf_complete)(dhd_pub_t *dhd, void* buf)
{
	info_buf_resp_t *resp;
	uint32 pktid;
	uint16 buflen;
	void * pkt;

	resp = (info_buf_resp_t *)buf;
	pktid = ltoh32(resp->cmn_hdr.request_id);
	buflen = ltoh16(resp->info_data_len);

#ifdef DHD_PKTID_AUDIT_RING
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_ctrl_map, pktid,
			DHD_DUPLICATE_FREE, buf, D2HRING_INFO_BUFCMPLT_ITEMSIZE);
#endif /* DHD_PKTID_AUDIT_RING */

	DHD_MSGBUF_INFO(("id 0x%04x, len %d, phase 0x%02x, seqnum %d, rx_dataoffset %d\n",
		pktid, buflen, resp->cmn_hdr.flags, ltoh16(resp->seqnum),
		dhd->prot->rx_dataoffset));

	if (dhd->debug_buf_dest_support) {
		if (resp->dest < DEBUG_BUF_DEST_MAX) {
			dhd->debug_buf_dest_stat[resp->dest]++;
		}
	}

	pkt = dhd_prot_packet_get(dhd, pktid, PKTTYPE_INFO_RX, TRUE);
	if (!pkt)
		return;

#if !defined(BCM_ROUTER_DHD)
	/* FIXME: make sure the length is more than dataoffset */
	/* DMA RX offset updated through shared area */
	if (dhd->prot->rx_dataoffset)
		PKTPULL(dhd->osh, pkt, dhd->prot->rx_dataoffset);
#endif /* !BCM_ROUTER_DHD */

	PKTSETLEN(dhd->osh, pkt, buflen);
#ifdef DHD_LBUF_AUDIT
	PKTAUDIT(dhd->osh, pkt);
#endif
	/* info ring "debug" data, which is not a 802.3 frame, is sent/hacked with a
	 * special ifidx of -1.  This is just internal to dhd to get the data to
	 * dhd_linux.c:dhd_rx_frame() from here (dhd_prot_infobuf_cmplt_process).
	 */
	dhd_bus_rx_frame(dhd->bus, pkt, DHD_DUMMY_INFO_IF /* ifidx HACK */, 1);
}
#endif /* !BCM_ROUTER_DHD */

/** called on MSG_TYPE_SNAPSHOT_CMPLT message received from dongle */
static void
BCMFASTPATH(dhd_prot_process_snapshot_complete)(dhd_pub_t *dhd, void *buf)
{
#ifdef SNAPSHOT_UPLOAD
	dhd_prot_t *prot = dhd->prot;
	snapshot_resp_t *resp;
	uint16 status;

	resp = (snapshot_resp_t *)buf;

	/* check completion status */
	status = resp->compl_hdr.status;
	if (status != BCMPCIE_SUCCESS) {
		DHD_ERROR(("%s: failed: %s (%d)\n",
			__FUNCTION__,
			status == BCMPCIE_BT_DMA_ERR ? "DMA_ERR" :
			status == BCMPCIE_BT_DMA_DESCR_FETCH_ERR ?
				"DMA_DESCR_ERR" :
			status == BCMPCIE_SNAPSHOT_ERR ? "SNAPSHOT_ERR" :
			status == BCMPCIE_NOT_READY ? "NOT_READY" :
			status == BCMPCIE_INVALID_DATA ? "INVALID_DATA" :
			status == BCMPCIE_NO_RESPONSE ? "NO_RESPONSE" :
			status == BCMPCIE_NO_CLOCK ? "NO_CLOCK" :
			"", status));
	}

	/* length may be truncated if error occurred */
	prot->snapshot_upload_len = ltoh32(resp->resp_len);
	prot->snapshot_type = resp->type;
	prot->snapshot_cmpl_pending = FALSE;

	DHD_INFO(("%s id 0x%04x, phase 0x%02x, resp_len %d, type %d\n",
		__FUNCTION__, ltoh32(resp->cmn_hdr.request_id),
		resp->cmn_hdr.flags,
		prot->snapshot_upload_len, prot->snapshot_type));
#endif	/* SNAPSHOT_UPLOAD */
}

#ifdef BTLOG
/** called on MSG_TYPE_BT_LOG_CMPLT message received from dongle */
static void
BCMFASTPATH(dhd_prot_process_btlog_complete)(dhd_pub_t *dhd, void* buf)
{
	info_buf_resp_t *resp;
	uint32 pktid;
	uint16 buflen;
	void * pkt;

	resp = (info_buf_resp_t *)buf;
	pktid = ltoh32(resp->cmn_hdr.request_id);
	buflen = ltoh16(resp->info_data_len);

	/* check completion status */
	if (resp->compl_hdr.status != BCMPCIE_SUCCESS) {
		DHD_ERROR(("%s: failed completion status %d\n",
			__FUNCTION__, resp->compl_hdr.status));
		return;
	}

#ifdef DHD_PKTID_AUDIT_RING
	DHD_PKTID_AUDIT_RING_DEBUG(dhd, dhd->prot->pktid_ctrl_map, pktid,
			DHD_DUPLICATE_FREE, buf, D2HRING_INFO_BUFCMPLT_ITEMSIZE);
#endif /* DHD_PKTID_AUDIT_RING */

	DHD_INFO(("id 0x%04x, len %d, phase 0x%02x, seqnum %d, rx_dataoffset %d\n",
		pktid, buflen, resp->cmn_hdr.flags, ltoh16(resp->seqnum),
		dhd->prot->rx_dataoffset));

	pkt = dhd_prot_packet_get(dhd, pktid, PKTTYPE_INFO_RX, TRUE);

	if (!pkt)
		return;

#if !defined(BCM_ROUTER_DHD)
	/* FIXME: make sure the length is more than dataoffset */
	/* DMA RX offset updated through shared area */
	if (dhd->prot->rx_dataoffset)
		PKTPULL(dhd->osh, pkt, dhd->prot->rx_dataoffset);
#endif /* !BCM_ROUTER_DHD */

	PKTSETLEN(dhd->osh, pkt, buflen);
	PKTSETNEXT(dhd->osh, pkt, NULL);

	dhd_bus_rx_bt_log(dhd->bus, pkt);
}
#endif	/* BTLOG */

/** Stop protocol: sync w/dongle state. */
void dhd_prot_stop(dhd_pub_t *dhd)
{
	ASSERT(dhd);
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#if defined(NDIS)
	if (dhd->prot) {
		DHD_NATIVE_TO_PKTID_RESET(dhd, dhd->prot->pktid_ctrl_map);
		DHD_NATIVE_TO_PKTID_RESET(dhd, dhd->prot->pktid_rx_map);
		DHD_NATIVE_TO_PKTID_RESET(dhd, dhd->prot->pktid_tx_map);
#if defined(IOCTLRESP_USE_CONSTMEM)
		DHD_NATIVE_TO_PKTID_RESET_IOCTL(dhd, dhd->prot->pktid_map_handle_ioctl);
#endif /* DHD_PCIE_PKTID */
	}
#endif /* NDIS */
}

/* Add any protocol-specific data header.
 * Caller must reserve prot_hdrlen prepend space.
 */
void
BCMFASTPATH(dhd_prot_hdrpush)(dhd_pub_t *dhd, int ifidx, void *PKTBUF)
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
int
BCMFASTPATH(dhd_prot_txdata)(dhd_pub_t *dhd, void *PKTBUF, uint8 ifidx)
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
#if defined(BCMINTERNAL) && defined(LINUX)
	void *pkt_to_free = NULL;
#endif /* BCMINTERNAL && LINUX */
#ifdef DHD_PKTTS
	dhd_dma_buf_t	meta_data_buf;
	uint16	meta_data_buf_len = dhd->pkt_metadata_buflen;
	uint64 addr = 0;
#endif /* DHD_PKTTS */
	void *big_pktbuf = NULL;
	uint8 dhd_udr = FALSE;
	bool host_sfh_llc_reqd = dhd->host_sfhllc_supported;
	bool llc_inserted = FALSE;

	BCM_REFERENCE(llc_inserted);
#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK) {
		DHD_ERROR(("failed to increment hostactive_devwake\n"));
		return BCME_ERROR;
	}
#endif /* PCIE_INB_DW */

	if (dhd->flow_ring_table == NULL) {
		DHD_ERROR(("dhd flow_ring_table is NULL\n"));
		goto fail;
	}

#ifdef DHD_PCIE_PKTID
		if (!DHD_PKTID_AVAIL(dhd->prot->pktid_tx_map)) {
			if (dhd->prot->pktid_depleted_cnt == DHD_PKTID_DEPLETED_MAX_COUNT) {
				DHD_ERROR(("%s: stop tx queue as pktid_depleted_cnt maxed\n",
					__FUNCTION__));
				prot->pktid_txq_stop_cnt++;
				dhd_bus_stop_queue(dhd->bus);
				dhd->prot->no_tx_resource = TRUE;
			}
			dhd->prot->pktid_depleted_cnt++;
			goto fail;
		} else {
			dhd->prot->pktid_depleted_cnt = 0;
		}
#endif /* DHD_PCIE_PKTID */

	if (dhd->dhd_induce_error == DHD_INDUCE_TX_BIG_PKT) {
		if ((big_pktbuf = PKTGET(dhd->osh, DHD_FLOWRING_TX_BIG_PKT_SIZE, TRUE)) == NULL) {
			DHD_ERROR(("%s:%d: PKTGET for txbuf failed\n", __FUNCTION__, __LINE__));
			goto fail;
		}

		memset(PKTDATA(dhd->osh, big_pktbuf), 0xff, DHD_FLOWRING_TX_BIG_PKT_SIZE);
		DHD_ERROR(("PKTBUF len = %d big_pktbuf len = %d\n", PKTLEN(dhd->osh, PKTBUF),
				PKTLEN(dhd->osh, big_pktbuf)));
		if (memcpy_s(PKTDATA(dhd->osh, big_pktbuf), DHD_FLOWRING_TX_BIG_PKT_SIZE,
				PKTDATA(dhd->osh, PKTBUF), PKTLEN(dhd->osh, PKTBUF)) != BCME_OK) {
			DHD_ERROR(("%s:%d: memcpy_s big_pktbuf failed\n", __FUNCTION__, __LINE__));
			ASSERT(0);
		}
	}

	flowid = DHD_PKT_GET_FLOWID(PKTBUF);
	flow_ring_table = (flow_ring_table_t *)dhd->flow_ring_table;
	flow_ring_node = (flow_ring_node_t *)&flow_ring_table[flowid];

	ring = (msgbuf_ring_t *)flow_ring_node->prot_info;

	/*
	 * XXX:
	 * JIRA SW4349-436:
	 * Copying the TX Buffer to an SKB that lives in the DMA Zone
	 * is done here. Previously this was done from dhd_stat_xmit
	 * On conditions where the Host is pumping heavy traffic to
	 * the dongle, we see that the Queue that is backing up the
	 * flow rings is getting full and holds the precious memory
	 * from DMA Zone, leading the host to run out of memory in DMA
	 * Zone. So after this change the back up queue would continue to
	 * hold the pointers from Network Stack, just before putting
	 * the PHY ADDR in the flow rings, we'll do the copy.
	 */
#if defined(BCMINTERNAL) && defined(LINUX)
	if (osl_is_flag_set(dhd->osh, OSL_PHYS_MEM_LESS_THAN_16MB)) {
		struct sk_buff *skb;
		/*
		 * We are about to add the Ethernet header and send out,
		 * copy the skb here.
		 */
		skb = skb_copy(PKTBUF, GFP_DMA);
		if (skb == NULL) {
			/*
			 * Memory allocation failed, the old packet can
			 * live in the queue, return BCME_NORESOURCE so
			 * the caller re-queues this packet
			 */
			DHD_ERROR(("%s: skb_copy(DMA) failed\n", __FUNCTION__));
			goto fail;
		}

		/*
		 * Now we have copied the SKB to GFP_DMA memory, make the
		 * rest of the code operate on this new SKB. Hold on to
		 * the original SKB. If we don't get the pkt id or flow ring
		 * space we'll free the Zone memory and return "no resource"
		 * so the caller would re-queue the original SKB.
		 */
		pkt_to_free = PKTBUF;
		PKTBUF = skb;
	}
#endif	/* BCMINTERNAL && LINUX */

	if (dhd->dhd_induce_error == DHD_INDUCE_TX_BIG_PKT && big_pktbuf) {
		PKTFREE(dhd->osh, PKTBUF, TRUE);
		PKTBUF = big_pktbuf;
	}

	DHD_RING_LOCK(ring->ring_lock, flags);

	/* Create a unique 32-bit packet id */
	pktid = DHD_NATIVE_TO_PKTID_RSV(dhd, dhd->prot->pktid_tx_map,
		PKTBUF, PKTTYPE_DATA_TX);
#if defined(DHD_PCIE_PKTID)
	if (pktid == DHD_PKTID_INVALID) {
		DHD_ERROR_RLMT(("%s: Pktid pool depleted.\n", __FUNCTION__));
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
			__FUNCTION__, __LINE__, OSL_ATOMIC_READ(dhd->osh, &prot->active_tx_count)));
		goto err_free_pktid;
	}
	txdesc->flags = 0;

	/* Extract the data pointer and length information */
	pktdata = PKTDATA(dhd->osh, PKTBUF);
	pktlen  = PKTLEN(dhd->osh, PKTBUF);

	/* TODO: XXX: re-look into dropped packets */
	DHD_DBG_PKT_MON_TX(dhd, PKTBUF, pktid);

	dhd_handle_pktdata(dhd, ifidx, PKTBUF, pktdata, pktid,
		pktlen, NULL, &dhd_udr, TRUE, FALSE, TRUE);

#if defined(BCMINTERNAL) && defined(LINUX)
	/*
	 * We have got all the resources, pktid and ring space
	 * so we can safely free the original SKB here.
	 */
	if (osl_is_flag_set(dhd->osh, OSL_PHYS_MEM_LESS_THAN_16MB))
		PKTCFREE(dhd->osh, pkt_to_free, FALSE);
#endif	/* BCMINTERNAL && LINUX */

	/* Ethernet header - contains ethertype field
	* Copy before we cache flush packet using DMA_MAP
	*/
	bcopy(pktdata, txdesc->txhdr, ETHER_HDR_LEN);

#ifdef DHD_AWDL
	/* the awdl ifidx will always have a non-zero value
	 * if the awdl iface is created. This is because the
	 * primary iface (usually eth1) will always have ifidx of 0.
	 * Hence we can check for non-zero value of awdl ifidx to
	 * see if awdl iface is created or not
	 */
	if (dhd->awdl_llc_enabled &&
		dhd->awdl_ifidx && ifidx == dhd->awdl_ifidx) {
		if (host_sfh_llc_reqd) {
			/* if FW supports host sfh llc insertion
			 * then BOTH sfh and llc needs to be inserted
			 * in which case the host LLC only path
			 * in FW will not be exercised - which is the
			 * objective of this feature. Hence in such a
			 * case disable awdl llc insertion
			 */
			DHD_ERROR_RLMT(("%s: FW supports host sfh + llc, this is"
				"is incompatible with awdl llc insertion"
				" disable host sfh llc support in FW and try\n",
				__FUNCTION__));
		} else {
			if (dhd_ether_to_awdl_llc_hdr(dhd, (struct ether_header *)pktdata,
				PKTBUF) == BCME_OK) {
			llc_inserted = TRUE;
			/* in work item change ether type to len by
			 * re-copying the ether header
			 */
			memcpy_s(txdesc->txhdr, ETHER_HDR_LEN, PKTDATA(dhd->osh, PKTBUF),
				ETHER_HDR_LEN);
			} else {
				goto err_rollback_idx;
			}
		}
	}
#endif /* DHD_AWDL */

#ifdef HOST_SFH_LLC
	if (host_sfh_llc_reqd) {
		if (dhd_ether_to_8023_hdr(dhd->osh, (struct ether_header *)pktdata,
				PKTBUF) == BCME_OK) {
			/* adjust the data pointer and length information */
			pktdata = PKTDATA(dhd->osh, PKTBUF);
			pktlen  = PKTLEN(dhd->osh, PKTBUF);
			txdesc->flags |= BCMPCIE_TXPOST_FLAGS_HOST_SFH_LLC;
		} else {
			goto err_rollback_idx;
		}
	} else
#endif /* HOST_SFH_LLC */
	{
		/* Extract the ethernet header and adjust the data pointer and length */
		pktlen = PKTLEN(dhd->osh, PKTBUF) - ETHER_HDR_LEN;
		pktdata = PKTPULL(dhd->osh, PKTBUF, ETHER_HDR_LEN);
	}

	/* Map the data pointer to a DMA-able address */
	pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, PKTBUF), pktlen, DMA_TX, PKTBUF, 0);

	if (PHYSADDRISZERO(pa)) {
		DHD_ERROR(("%s: Something really bad, unless 0 is "
			"a valid phyaddr for pa\n", __FUNCTION__));
		ASSERT(0);
		/* XXX if ASSERT() doesn't work like as Android platform,
		 * try to requeue the packet to the backup queue.
		 */
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
#ifdef DHD_HMAPTEST
	if (dhd->prot->hmaptest_tx_active == HMAPTEST_D11_TX_ACTIVE) {
		/* scratch area */
		dhd->prot->hmap_tx_buf_va = (char *)dhd->prot->hmaptest.mem.va
			+ dhd->prot->hmaptest.offset;
		/* replace pa with our pa for txbuf post only */
		dhd->prot->hmap_tx_buf_len = pktlen;
		if ((dhd->prot->hmap_tx_buf_va + dhd->prot->hmap_tx_buf_len) >
			((char *)dhd->prot->hmaptest.mem.va + dhd->prot->hmaptest.mem.len)) {
			DHD_ERROR(("hmaptest: ERROR Txpost outside HMAPTEST buffer\n"));
			DHD_ERROR(("hmaptest: NOT Replacing Rx Buffer\n"));
			dhd->prot->hmaptest_tx_active = HMAPTEST_D11_TX_INACTIVE;
			dhd->prot->hmaptest.in_progress = FALSE;
		} else {
			/* copy pktdata to our va */
			memcpy(dhd->prot->hmap_tx_buf_va, PKTDATA(dhd->osh, PKTBUF), pktlen);
			pa = DMA_MAP(dhd->osh, dhd->prot->hmap_tx_buf_va,
				dhd->prot->hmap_tx_buf_len, DMA_TX, PKTBUF, 0);

			dhd->prot->hmap_tx_buf_pa = pa;
			/* store pktid for later mapping in txcpl */
			dhd->prot->hmaptest_tx_pktid = pktid;
			dhd->prot->hmaptest_tx_active = HMAPTEST_D11_TX_POSTED;
			DHD_ERROR(("hmaptest: d11read txpost scratch txbuf pktid=0x%08x\n", pktid));
			DHD_ERROR(("hmaptest: d11read txpost txbuf va=0x%p pa.lo=0x%08x len=%d\n",
				dhd->prot->hmap_tx_buf_va, (uint32)PHYSADDRLO(pa), pktlen));
		}
	}
#endif /* DHD_HMAPTEST */

#ifdef DHD_PKTTS
	memset(&meta_data_buf, 0, sizeof(meta_data_buf));
	if (dhd_get_pktts_enab(dhd) &&
		dhd->pkt_metadata_buflen) {
		/* Allocate memory for Meta data */
		meta_data_buf.va = DMA_ALLOC_CONSISTENT(dhd->osh, meta_data_buf_len,
			DMA_ALIGN_LEN, &meta_data_buf._alloced,
			&meta_data_buf.pa, &meta_data_buf.dmah);

		if (meta_data_buf.va == NULL) {
			DHD_ERROR_RLMT(("%s: dhd_dma_buf_alloc failed \r\n", __FUNCTION__));
			DHD_ERROR_RLMT((" ... Proceeding without metadata buffer \r\n"));
		} else {
			DHD_PKTID_SAVE_METADATA(dhd, dhd->prot->pktid_tx_map,
				(void *)meta_data_buf.va,
				meta_data_buf.pa,
				(uint16)meta_data_buf._alloced,
				meta_data_buf.dmah,
				pktid);
		}
		memcpy(&addr, &meta_data_buf.pa, sizeof(meta_data_buf.pa));
		DHD_TRACE(("Meta data Buffer VA: %p  PA: %llx dmah: %p\r\n",
			meta_data_buf.va, addr, meta_data_buf.dmah));

		txdesc->metadata_buf_addr.low = addr & (0xFFFFFFFF);
		txdesc->metadata_buf_addr.high = (addr >> 32) & (0xFFFFFFFF);
		txdesc->metadata_buf_len = meta_data_buf_len;
	}
#endif /* DHD_PKTTS */

	/* Form the Tx descriptor message buffer */

	/* Common message hdr */
	txdesc->cmn_hdr.msg_type = MSG_TYPE_TX_POST;
	txdesc->cmn_hdr.if_id = ifidx;
	txdesc->cmn_hdr.flags = ring->current_phase;

	txdesc->flags |= BCMPCIE_PKT_FLAGS_FRAME_802_3;
	prio = (uint8)PKTPRIO(PKTBUF);

#ifdef EXT_STA
	txdesc->flags &= ~BCMPCIE_PKT_FLAGS_FRAME_EXEMPT_MASK <<
		BCMPCIE_PKT_FLAGS_FRAME_EXEMPT_SHIFT;
	txdesc->flags |= (WLPKTFLAG_EXEMPT_GET(WLPKTTAG(PKTBUF)) &
		BCMPCIE_PKT_FLAGS_FRAME_EXEMPT_MASK)
		<< BCMPCIE_PKT_FLAGS_FRAME_EXEMPT_SHIFT;
#endif

	txdesc->flags |= (prio & 0x7) << BCMPCIE_PKT_FLAGS_PRIO_SHIFT;
	txdesc->seg_cnt = 1;

	txdesc->data_len = htol16((uint16) pktlen);
	txdesc->data_buf_addr.high_addr = htol32(PHYSADDRHI(pa));
	txdesc->data_buf_addr.low_addr  = htol32(PHYSADDRLO(pa));

	if (!host_sfh_llc_reqd)
	{
		/* Move data pointer to keep ether header in local PKTBUF for later reference */
		PKTPUSH(dhd->osh, PKTBUF, ETHER_HDR_LEN);
	}

	txdesc->ext_flags = 0;

#ifdef DHD_TIMESYNC
	txdesc->rate = 0;

	if (!llc_inserted && dhd->prot->tx_ts_log_enabled) {
		dhd_pkt_parse_t parse;

		dhd_parse_proto(PKTDATA(dhd->osh, PKTBUF), &parse);

		if (parse.proto == IP_PROT_ICMP) {
			if (dhd->prot->no_retry)
				txdesc->ext_flags = BCMPCIE_PKT_FLAGS_FRAME_NORETRY;
			if (dhd->prot->no_aggr)
				txdesc->ext_flags |= BCMPCIE_PKT_FLAGS_FRAME_NOAGGR;
			if (dhd->prot->fixed_rate)
				txdesc->ext_flags |= BCMPCIE_PKT_FLAGS_FRAME_UDR;
		}
	}
#endif /* DHD_TIMESYNC */

#ifdef DHD_SBN
	if (dhd_udr) {
		txdesc->ext_flags |= BCMPCIE_PKT_FLAGS_FRAME_UDR;
	}
#endif /* DHD_SBN */

#ifdef DHD_TX_PROFILE
	if (!llc_inserted &&
		dhd->tx_profile_enab && dhd->num_profiles > 0)
	{
		uint8 offset;

		for (offset = 0; offset < dhd->num_profiles; offset++) {
			if (dhd_protocol_matches_profile((uint8 *)PKTDATA(dhd->osh, PKTBUF),
				PKTLEN(dhd->osh, PKTBUF), &(dhd->protocol_filters[offset]),
				host_sfh_llc_reqd)) {
				/* mask so other reserved bits are not modified. */
				txdesc->rate |=
					(((uint8)dhd->protocol_filters[offset].profile_index) &
					BCMPCIE_TXPOST_RATE_PROFILE_IDX_MASK);

				/* so we can use the rate field for our purposes */
				txdesc->rate |= BCMPCIE_TXPOST_RATE_EXT_USAGE;

				break;
			}
		}
	}
#endif /* defined(DHD_TX_PROFILE) */

	/* Handle Tx metadata */
	headroom = (uint16)PKTHEADROOM(dhd->osh, PKTBUF);
	if (prot->tx_metadata_offset && (headroom < prot->tx_metadata_offset))
		DHD_ERROR(("No headroom for Metadata tx %d %d\n",
		prot->tx_metadata_offset, headroom));

	if (prot->tx_metadata_offset && (headroom >= prot->tx_metadata_offset)) {
		DHD_TRACE(("Metadata in tx %d\n", prot->tx_metadata_offset));

		/* Adjust the data pointer to account for meta data in DMA_MAP */
		PKTPUSH(dhd->osh, PKTBUF, prot->tx_metadata_offset);

		meta_pa = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, PKTBUF),
			prot->tx_metadata_offset, DMA_RX, PKTBUF, 0);

		if (PHYSADDRISZERO(meta_pa)) {
			/* Unmap the data pointer to a DMA-able address */
			DMA_UNMAP(dhd->osh, pa, pktlen, DMA_TX, 0, DHD_DMAH_NULL);
#ifdef TXP_FLUSH_NITEMS
			/* update pend_items_count */
			ring->pend_items_count--;
#endif /* TXP_FLUSH_NITEMS */

			DHD_ERROR(("%s: Something really bad, unless 0 is "
				"a valid phyaddr for meta_pa\n", __FUNCTION__));
			ASSERT(0);
			/* XXX if ASSERT() doesn't work like as Android platform,
			 * try to requeue the packet to the backup queue.
			 */
			goto err_rollback_idx;
		}

		/* Adjust the data pointer back to original value */
		PKTPULL(dhd->osh, PKTBUF, prot->tx_metadata_offset);

		txdesc->metadata_buf_len = prot->tx_metadata_offset;
		txdesc->metadata_buf_addr.high_addr = htol32(PHYSADDRHI(meta_pa));
		txdesc->metadata_buf_addr.low_addr = htol32(PHYSADDRLO(meta_pa));
	} else {
#ifdef DHD_HP2P
		if (dhd->hp2p_capable && flow_ring_node->flow_info.tid == HP2P_PRIO) {
			dhd_update_hp2p_txdesc(dhd, txdesc);
		} else
#endif /* DHD_HP2P */
#ifdef DHD_PKTTS
		if (!dhd_get_pktts_enab(dhd) || !dhd->pkt_metadata_buflen) {
#else
		if (1) {
#endif /* DHD_PKTTS */
			txdesc->metadata_buf_len = htol16(0);
			txdesc->metadata_buf_addr.high_addr = 0;
			txdesc->metadata_buf_addr.low_addr = 0;
		}
	}

#ifdef AGG_H2D_DB
	OSL_ATOMIC_INC(dhd->osh, &ring->inflight);
#endif /* AGG_H2D_DB */

#ifdef DHD_PKTID_AUDIT_RING
	DHD_PKTID_AUDIT(dhd, prot->pktid_tx_map, pktid, DHD_DUPLICATE_ALLOC);
#endif /* DHD_PKTID_AUDIT_RING */

	txdesc->cmn_hdr.request_id = htol32(pktid);

	DHD_TRACE(("txpost: data_len %d, pktid 0x%04x\n", txdesc->data_len,
		txdesc->cmn_hdr.request_id));

#ifdef DHD_LBUF_AUDIT
	PKTAUDIT(dhd->osh, PKTBUF);
#endif

	/* Update the write pointer in TCM & ring bell */
#if defined(TXP_FLUSH_NITEMS)
#if defined(DHD_HP2P)
	if (dhd->hp2p_capable && flow_ring_node->flow_info.tid == HP2P_PRIO) {
		dhd_calc_hp2p_burst(dhd, ring, flowid);
	} else
#endif /* HP2P */
	{
		if ((ring->pend_items_count == prot->txp_threshold) ||
				((uint8 *) txdesc == (uint8 *) DHD_RING_END_VA(ring))) {
#ifdef AGG_H2D_DB
			if (agg_h2d_db_enab) {
				dhd_prot_txdata_aggr_db_write_flush(dhd, flowid);
				if ((uint8 *) txdesc == (uint8 *) DHD_RING_END_VA(ring)) {
					dhd_prot_aggregate_db_ring_door_bell(dhd, flowid, TRUE);
				}
			} else
#endif /* AGG_H2D_DB */
			{
				dhd_prot_txdata_write_flush(dhd, flowid);
			}

		}
	}
#else
	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, txdesc, 1);
#endif /* TXP_FLUSH_NITEMS */

#ifdef TX_STATUS_LATENCY_STATS
	/* set the time when pkt is queued to flowring */
	DHD_PKT_SET_QTIME(PKTBUF, OSL_SYSUPTIME_US());
#elif defined(DHD_PKTTS)
	if (dhd_get_pktts_enab(dhd) == TRUE) {
		/* set the time when pkt is queued to flowring */
		DHD_PKT_SET_QTIME(PKTBUF, OSL_SYSUPTIME_US());
	}
#endif /* TX_STATUS_LATENCY_STATS */

	DHD_RING_UNLOCK(ring->ring_lock, flags);

	OSL_ATOMIC_INC(dhd->osh, &prot->active_tx_count);

	/*
	 * Take a wake lock, do not sleep if we have atleast one packet
	 * to finish.
	 */
	DHD_TXFL_WAKE_LOCK_TIMEOUT(dhd, MAX_TX_TIMEOUT);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
#ifdef TX_STATUS_LATENCY_STATS
	flow_ring_node->flow_info.num_tx_pkts++;
#endif /* TX_STATUS_LATENCY_STATS */
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

#if defined(BCMINTERNAL) && defined(LINUX)
	if (osl_is_flag_set(dhd->osh, OSL_PHYS_MEM_LESS_THAN_16MB))
		PKTCFREE(dhd->osh, PKTBUF, FALSE);
#endif	/* BCMINTERNAL && LINUX */

	DHD_RING_UNLOCK(ring->ring_lock, flags);

fail:
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return BCME_NORESOURCE;
} /* dhd_prot_txdata */

#ifdef AGG_H2D_DB
static void
dhd_prot_txdata_aggr_db_write_flush(dhd_pub_t *dhd, uint16 flowid)
{
	flow_ring_table_t *flow_ring_table;
	flow_ring_node_t *flow_ring_node;
	msgbuf_ring_t *ring;

	if (dhd->flow_ring_table == NULL) {
		return;
	}

	flow_ring_table = (flow_ring_table_t *)dhd->flow_ring_table;
	flow_ring_node = (flow_ring_node_t *)&flow_ring_table[flowid];
	ring = (msgbuf_ring_t *)flow_ring_node->prot_info;

	if (ring->pend_items_count) {
		dhd_prot_agg_db_ring_write(dhd, ring, ring->start_addr,
				ring->pend_items_count);
		ring->pend_items_count = 0;
		ring->start_addr = NULL;
	}

}
#endif /* AGG_H2D_DB */

/* called with a ring_lock */
/** optimization to write "n" tx items at a time to ring */
void
BCMFASTPATH(dhd_prot_txdata_write_flush)(dhd_pub_t *dhd, uint16 flowid)
{
#ifdef TXP_FLUSH_NITEMS
	flow_ring_table_t *flow_ring_table;
	flow_ring_node_t *flow_ring_node;
	msgbuf_ring_t *ring;

	if (dhd->flow_ring_table == NULL) {
		return;
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
		dhd->prot->tx_h2d_db_cnt++;
	}
#endif /* TXP_FLUSH_NITEMS */
}

#undef PKTBUF	/* Only defined in the above routine */

int
BCMFASTPATH(dhd_prot_hdrpull)(dhd_pub_t *dhd, int *ifidx, void *pkt, uchar *buf, uint *len)
{
	return 0;
}

/** post a set of receive buffers to the dongle */
static void
BCMFASTPATH(dhd_prot_return_rxbuf)(dhd_pub_t *dhd, msgbuf_ring_t *ring, uint32 pktid,
	uint32 rxcnt)
/* XXX function name could be more descriptive, eg dhd_prot_post_rxbufs */
{
	dhd_prot_t *prot = dhd->prot;

	if (prot->rxbufpost >= rxcnt) {
		prot->rxbufpost -= (uint16)rxcnt;
	} else {
		/* XXX: I have seen this assert hitting.
		 * Will be removed once rootcaused.
		 */
		/* ASSERT(0); */
		prot->rxbufpost = 0;
	}

	if (prot->rxbufpost <= (prot->max_rxbufpost - RXBUFPOST_THRESHOLD)) {
		dhd_msgbuf_rxbuf_post(dhd, FALSE); /* alloc pkt ids */
	} else if (dhd->dma_h2d_ring_upd_support && !IDMA_ACTIVE(dhd)) {
		/* Ring DoorBell after processing the rx packets,
		 * so that dongle will sync the DMA indices.
		 */
		dhd_prot_ring_doorbell(dhd, DHD_RDPTR_UPDATE_H2D_DB_MAGIC(ring));
	}

	return;
}

#ifdef DHD_HMAPTEST

static void
dhd_msgbuf_hmaptest_cmplt(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	uint64 end_usec;
	char *readbuf;
	uint32 len = dhd->prot->hmaptest.len;
	uint32 i;

	end_usec = OSL_SYSUPTIME_US();
	end_usec -= prot->hmaptest.start_usec;
	DHD_ERROR(("hmaptest cmplt: %d bytes in %llu usec, %u kBps\n",
		len, end_usec, (len * (1000 * 1000 / 1024) / (uint32)(end_usec + 1))));

	prot->hmaptest.in_progress = FALSE;
	if (prot->hmaptest.accesstype == HMAPTEST_ACCESS_M2M) {
			DHD_ERROR(("HMAPTEST_ACCESS_M2M\n"));
	} else if (prot->hmaptest.accesstype == HMAPTEST_ACCESS_ARM) {
			DHD_ERROR(("HMAPTEST_ACCESS_ARM\n"));
	} else {
		return;
	}
	readbuf = (char *)dhd->prot->hmaptest.mem.va + dhd->prot->hmaptest.offset;
	OSL_CACHE_FLUSH(dhd->prot->hmaptest.mem.va,
		dhd->prot->hmaptest.mem.len);
	if (prot->hmaptest.is_write) {
		DHD_ERROR(("hmaptest cmplt: FW has written at 0x%p\n", readbuf));
		DHD_ERROR(("hmaptest cmplt: pattern = \n"));
		len = ALIGN_SIZE(len, (sizeof(int32)));
		for (i = 0; i < len; i += (sizeof(int32))) {
			DHD_ERROR(("0x%08x\n", *(int *)(readbuf + i)));
		}
		DHD_ERROR(("\n\n"));
	}

}
/* program HMAPTEST window and window config registers
 * Reference for HMAP implementation in OS's that can easily leverage it
 * this function can be used as reference for programming HMAP windows
 * the function to program HMAP windows and enable it
 * can be called at init time or hmap iovar
 */
static void
dhdmsgbuf_set_hmaptest_windows(dhd_pub_t *dhd)
{
	uint32 nwindows = 0;
	uint32 scratch_len;
	uint64 scratch_lin, w1_start;
	dmaaddr_t scratch_pa;
	pcie_hmapwindow_t *hmapwindows; /* 8 windows 0-7 */
	dhd_prot_t *prot = dhd->prot;
	uint corerev = dhd->bus->sih->buscorerev;

	scratch_pa = prot->hmaptest.mem.pa;
	scratch_len = prot->hmaptest.mem.len;
	scratch_lin  = (uint64)(PHYSADDRLO(scratch_pa) & 0xffffffff)
		| (((uint64)PHYSADDRHI(scratch_pa)& 0xffffffff) << 32);
	hmapwindows = (pcie_hmapwindow_t *)((uintptr_t)PCI_HMAP_WINDOW_BASE(corerev));
	/* windows are 4kb aligned and window length is 512 byte aligned
	 * window start ends with 0x1000 and window length ends with 0xe00
	 * make the sandbox buffer 4kb aligned and size also 4kb aligned for hmap test
	 * window0 = 0 - sandbox_start
	 * window1 = sandbox_end + 1 - 0xffffffff
	 * window2 = 0x100000000 - 0x1fffffe00
	 * window 3 is programmed only for valid test cases
	 * window3 = sandbox_start - sandbox_end
	 */
	w1_start  = scratch_lin +  scratch_len;
		DHD_ERROR(("hmaptest: window 0 offset lower=0x%p upper=0x%p length=0x%p\n",
		&(hmapwindows[0].baseaddr_lo), &(hmapwindows[0].baseaddr_hi),
		&(hmapwindows[0].windowlength)));
	DHD_ERROR(("hmaptest: window 1 offset lower=0x%p upper=0x%p length=0x%p\n",
		&(hmapwindows[1].baseaddr_lo), &(hmapwindows[1].baseaddr_hi),
		&(hmapwindows[1].windowlength)));
	DHD_ERROR(("hmaptest: window 2 offset lower=0x%p upper=0x%p length=0x%p\n",
		&(hmapwindows[2].baseaddr_lo), &(hmapwindows[2].baseaddr_hi),
			&(hmapwindows[2].windowlength)));
	DHD_ERROR(("hmaptest: window 3 offset lower=0x%p upper=0x%p length=0x%p\n",
		&(hmapwindows[3].baseaddr_lo), &(hmapwindows[3].baseaddr_hi),
		&(hmapwindows[3].windowlength)));
		DHD_ERROR(("hmaptest: w0 base_lo=0x%08x base_hi=0x%08x len=0x%0llx\n",
			0, 0, (uint64) scratch_lin));
		DHD_ERROR(("hmaptest: w1 base_lo=0x%08x base_hi=0x%08x len=0x%0llx\n",
			(uint32)(w1_start & 0xffffffff),
			(uint32)((w1_start >> 32) & 0xffffffff),
			(uint64)(0x100000000 - w1_start)));
		DHD_ERROR(("hmaptest: w2 base_lo=0x%08x base_hi=0x%08x len=0x%0llx\n",
			0, 1, (uint64)0xfffffe00));
		/* setting window0 */
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[0].baseaddr_lo)), ~0, 0x0);
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[0].baseaddr_hi)), ~0, 0x0);
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[0].windowlength)), ~0,
			(uint64)scratch_lin);
		/* setting window1 */
		w1_start  = scratch_lin +  scratch_len;
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[1].baseaddr_lo)), ~0,
			(uint32)(w1_start & 0xffffffff));
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[1].baseaddr_hi)), ~0,
			(uint32)((w1_start >> 32) & 0xffffffff));
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[1].windowlength)), ~0,
			(0x100000000 - w1_start));
		/* setting window2 */
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[2].baseaddr_lo)), ~0, 0x0);
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[2].baseaddr_hi)), ~0, 0x1);
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[2].windowlength)), ~0, 0xfffffe00);
		nwindows = 3;
		/* program only windows 0-2 with section1 +section2 */
		/* setting window config */
		/* set bit 8:15 in windowconfig to enable n windows in order */
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uint)PCI_HMAP_WINDOW_CONFIG(corerev), ~0, (nwindows << 8));
}

/* stop HMAPTEST does not check corerev
 * caller has to ensure corerev check
 */
int
dhdmsgbuf_hmaptest_stop(dhd_pub_t *dhd)
{
	uint32 window_config, nwindows, i;
	pcie_hmapwindow_t *hmapwindows; /* 8 windows 0-7 */
	uint corerev = dhd->bus->sih->buscorerev;

	hmapwindows = (pcie_hmapwindow_t *)((uintptr_t)PCI_HMAP_WINDOW_BASE(corerev));
	dhd->prot->hmaptest.in_progress = FALSE;

	/* Reference for HMAP Implementation
	 * Disable HMAP windows.
	 * As windows were programmed in bus:hmap set call
	 * disabling in hmaptest_stop.
	 */
	DHD_ERROR(("hmap: disable hmap windows\n"));
	window_config = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		(uint)PCI_HMAP_WINDOW_CONFIG(corerev), 0, 0);
	nwindows = (window_config & PCI_HMAP_NWINDOWS_MASK) >> PCI_HMAP_NWINDOWS_SHIFT;
	si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		(uint)PCI_HMAP_WINDOW_CONFIG(corerev), ~0, 0);
	/* clear all windows */
	for (i = 0; i < nwindows; i++) {
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[i].baseaddr_lo)), ~0, 0);
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[i].baseaddr_hi)), ~0, 0);
		si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[i].windowlength)), ~0, 0);
	}

	return BCME_OK;
}

/* HMAP iovar intercept process */
int
dhdmsgbuf_hmap(dhd_pub_t *dhd, pcie_hmap_t *hmap_params, bool set)
{

	uint32 scratch_len;
	uint64 scratch_lin, w1_start;
	dmaaddr_t scratch_pa;
	uint32 addr_lo, addr_hi, window_length, window_config, nwindows, i;
	pcie_hmapwindow_t *hmapwindows; /* 8 windows 0-7 */

	dhd_prot_t *prot = dhd->prot;
	dhd_bus_t *bus = dhd->bus;
	uint corerev = bus->sih->buscorerev;
	scratch_pa = prot->hmaptest.mem.pa;
	scratch_len = prot->hmaptest.mem.len;
	scratch_lin  = (uint64)(PHYSADDRLO(scratch_pa) & 0xffffffff)
		| (((uint64)PHYSADDRHI(scratch_pa)& 0xffffffff) << 32);
	w1_start  = scratch_lin +  scratch_len;
	DHD_ERROR(("HMAP:  pcicorerev = %d\n", corerev));

	if (corerev < 24) {
		DHD_ERROR(("HMAP not available on pci corerev = %d\n", corerev));
		return BCME_UNSUPPORTED;
	}
	if (set) {
		if (hmap_params->enable) {
			dhdmsgbuf_set_hmaptest_windows(dhd);
		} else {
			dhdmsgbuf_hmaptest_stop(dhd); /* stop will clear all programmed windows */
		}
	}

	OSL_CACHE_FLUSH(dhd->prot->hmaptest.mem.va,
		dhd->prot->hmaptest.mem.len);

	window_config = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		(uint)PCI_HMAP_WINDOW_CONFIG(corerev), 0, 0);
	nwindows = (window_config & PCI_HMAP_NWINDOWS_MASK) >> PCI_HMAP_NWINDOWS_SHIFT;
	prot->hmap_enabled = nwindows ? TRUE : FALSE;

	/* getting window config */
	/* set bit 8:15 in windowconfig to enable n windows in order */
	DHD_ERROR(("hmap: hmap status = %s\n", (prot->hmap_enabled ? "Enabled" : "Disabled")));
	DHD_ERROR(("hmap: window config = 0x%08x\n", window_config));
	DHD_ERROR(("hmap: Windows\n"));

	hmapwindows = (pcie_hmapwindow_t *)((uintptr_t)PCI_HMAP_WINDOW_BASE(corerev));
	/* getting windows */
	if (nwindows > 8)
		return BCME_ERROR;
	for (i = 0; i < nwindows; i++) {
		addr_lo = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[i].baseaddr_lo)), 0, 0);
		addr_hi = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[i].baseaddr_hi)), 0, 0);
		window_length = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
			(uintptr_t)(&(hmapwindows[i].windowlength)), 0, 0);

		DHD_ERROR(("hmap: window %d address lower=0x%08x upper=0x%08x length=0x%08x\n",
			i, addr_lo, addr_hi, window_length));
	}
	addr_hi = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		(uint)(PCI_HMAP_VIOLATION_ADDR_U(corerev)), 0, 0);
	addr_lo = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		(uint)(PCI_HMAP_VIOLATION_ADDR_L(corerev)), 0, 0);
	window_length = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx,
		(uint)(PCI_HMAP_VIOLATION_INFO(corerev)), 0, 0);
	DHD_ERROR(("hmap: violation regs\n"));
	DHD_ERROR(("hmap: violationaddr_hi =0x%08x\n", addr_hi));
	DHD_ERROR(("hmap: violationaddr_lo =0x%08x\n", addr_lo));
	DHD_ERROR(("hmap: violation_info   =0x%08x\n", window_length));
	DHD_ERROR(("hmap: Buffer allocated for HMAPTEST Start=0x%0llx len =0x%08x End =0x%0llx\n",
		(uint64) scratch_lin, scratch_len, (uint64) w1_start));

	return BCME_OK;
}

/* hmaptest iovar process
 * This iovar triggers HMAPTEST with given params
 * on chips that have HMAP
 * DHD programs hmap window registers with host addresses here.
 */
int
dhdmsgbuf_hmaptest(dhd_pub_t *dhd, pcie_hmaptest_t *hmaptest_params)
{

	dhd_prot_t *prot = dhd->prot;
	int ret = BCME_OK;
	uint32 offset = 0;
	uint64 scratch_lin;
	dhd_bus_t *bus = dhd->bus;
	uint corerev = bus->sih->buscorerev;

	if (prot->hmaptest.in_progress) {
		DHD_ERROR(("HMAPTEST already running. Try again.\n"));
		return BCME_BUSY;
	}

	prot->hmaptest.in_progress = TRUE;

	if (corerev < 24) {
		DHD_ERROR(("HMAP not available on pci corerev = %d\n", corerev));
		return BCME_UNSUPPORTED;
	}
	prot->hmaptest.accesstype = hmaptest_params->accesstype;
	prot->hmaptest.is_write = hmaptest_params->is_write;
	prot->hmaptest.len = hmaptest_params->xfer_len;
	prot->hmaptest.offset = hmaptest_params->host_offset;
	offset = prot->hmaptest.offset;

	DHD_ERROR(("hmaptest: is_write =%d accesstype=%d offset =%d len=%d value=0x%08x\n",
		prot->hmaptest.is_write, prot->hmaptest.accesstype,
		offset, prot->hmaptest.len, hmaptest_params->value));

	DHD_ERROR(("hmaptest  dma_lo=0x%08x hi=0x%08x pa\n",
		(uint32)PHYSADDRLO(prot->hmaptest.mem.pa),
		(uint32)PHYSADDRHI(prot->hmaptest.mem.pa)));

	if (prot->hmaptest.accesstype == HMAPTEST_ACCESS_D11) {
		if (prot->hmaptest.is_write) {
			/* if d11 is writing then post rxbuf from scratch area */
			dhd->prot->hmaptest_rx_active = HMAPTEST_D11_RX_ACTIVE;
		} else {
			/* if d11 is reading then post txbuf from scratch area */
			dhd->prot->hmaptest_tx_active = HMAPTEST_D11_TX_ACTIVE;
		}

	} else {
		uint32 pattern = 0xdeadbeef;
		uint32 i;
		uint32 maxbuflen = MIN(prot->hmaptest.len, (PKTBUFSZ));
		char *fillbuf = (char *)dhd->prot->hmaptest.mem.va
			+ offset;
		if ((fillbuf + maxbuflen) >
			((char *)dhd->prot->hmaptest.mem.va + dhd->prot->hmaptest.mem.len)) {
			DHD_ERROR(("hmaptest: M2m/ARM ERROR offset + len outside buffer\n"));
			dhd->prot->hmaptest.in_progress = FALSE;
			return BCME_BADARG;
		}

		if (prot->hmaptest.accesstype == HMAPTEST_ACCESS_M2M) {
			DHD_ERROR(("HMAPTEST_ACCESS_M2M\n"));
		} else if (prot->hmaptest.accesstype == HMAPTEST_ACCESS_ARM) {
			DHD_ERROR(("HMAPTEST_ACCESS_ARM\n"));
		} else {
			prot->hmaptest.in_progress = FALSE;
			DHD_ERROR(("hmaptest: accesstype error\n"));
			return BCME_BADARG;
		}

		/* fill a pattern at offset */
		maxbuflen = ALIGN_SIZE(maxbuflen, (sizeof(uint32)));
		memset(fillbuf, 0, maxbuflen);
		DHD_ERROR(("hmaptest: dhd write pattern at addr=0x%p\n",
			fillbuf));
		DHD_ERROR(("pattern = %08x, %u times",
			pattern, (uint32)(maxbuflen / sizeof(uint32))));
		for (i = 0; i < maxbuflen; i += sizeof(uint32)) {
			*(uint32 *)(fillbuf + i) = pattern;
		}
		OSL_CACHE_FLUSH(dhd->prot->hmaptest.mem.va,
			dhd->prot->hmaptest.mem.len);
		DHD_ERROR(("\n\n"));

	}

	/*
	 * Do not calculate address from scratch buffer + offset,
	 * if user supplied absolute address
	 */
	if (hmaptest_params->host_addr_lo || hmaptest_params->host_addr_hi) {
		if (prot->hmaptest.accesstype == HMAPTEST_ACCESS_D11) {
			DHD_ERROR(("hmaptest: accesstype D11 does not support absolute addr\n"));
			return BCME_UNSUPPORTED;
		}
	} else {
		scratch_lin  = (uint64)(PHYSADDRLO(prot->hmaptest.mem.pa) & 0xffffffff)
			| (((uint64)PHYSADDRHI(prot->hmaptest.mem.pa) & 0xffffffff) << 32);
		scratch_lin += offset;
		hmaptest_params->host_addr_lo = htol32((uint32)(scratch_lin & 0xffffffff));
		hmaptest_params->host_addr_hi = htol32((uint32)((scratch_lin >> 32) & 0xffffffff));
	}

	DHD_INFO(("HMAPTEST Started...\n"));
	prot->hmaptest.start_usec = OSL_SYSUPTIME_US();
	return ret;

}

#endif /* DHD_HMAPTEST */

/* called before an ioctl is sent to the dongle */
static void
dhd_prot_wlioctl_intercept(dhd_pub_t *dhd, wl_ioctl_t * ioc, void * buf)
{
	dhd_prot_t *prot = dhd->prot;
	int slen = 0;

	if (ioc->cmd == WLC_SET_VAR && buf != NULL && !strcmp(buf, "pcie_bus_tput")) {
		pcie_bus_tput_params_t *tput_params;

		slen = strlen("pcie_bus_tput") + 1;
		tput_params = (pcie_bus_tput_params_t*)((char *)buf + slen);
		bcopy(&prot->host_bus_throughput_buf.pa, &tput_params->host_buf_addr,
			sizeof(tput_params->host_buf_addr));
		tput_params->host_buf_len = DHD_BUS_TPUT_BUF_LEN;
	}

#ifdef DHD_HMAPTEST
	if (buf != NULL && !strcmp(buf, "bus:hmap")) {
		pcie_hmap_t *hmap_params;
		slen = strlen("bus:hmap") + 1;
		hmap_params = (pcie_hmap_t*)((char *)buf + slen);
		dhdmsgbuf_hmap(dhd, hmap_params, (ioc->cmd == WLC_SET_VAR));
	}

	if (ioc->cmd == WLC_SET_VAR && buf != NULL && !strcmp(buf, "bus:hmaptest")) {
		pcie_hmaptest_t *hmaptest_params;

		slen = strlen("bus:hmaptest") + 1;
		hmaptest_params = (pcie_hmaptest_t*)((char *)buf + slen);
		dhdmsgbuf_hmaptest(dhd, hmaptest_params);
	}
#endif /* DHD_HMAPTEST */
}

/* called after an ioctl returns from dongle */
static void
dhd_prot_wl_ioctl_ret_intercept(dhd_pub_t *dhd, wl_ioctl_t * ioc, void * buf,
	int ifidx, int ret, int len)
{

#ifdef DHD_HMAPTEST
	if (ioc->cmd == WLC_SET_VAR && buf != NULL && !strcmp(buf, "bus:hmaptest")) {
		dhd_msgbuf_hmaptest_cmplt(dhd);
	}
#endif /* DHD_HMAPTEST */

	if (!ret && ioc->cmd == WLC_SET_VAR && buf != NULL) {
		int slen;
		/* Intercept the wme_dp ioctl here */
		if (!strcmp(buf, "wme_dp")) {
			int val = 0;
			slen = strlen("wme_dp") + 1;
			if (len >= (int)(slen + sizeof(int)))
				bcopy(((char *)buf + slen), &val, sizeof(int));
			dhd->wme_dp = (uint8) ltoh32(val);
		}

#ifdef DHD_AWDL
		/* Intercept the awdl_peer_op ioctl here */
		if (!strcmp(buf, "awdl_peer_op")) {
			slen = strlen("awdl_peer_op") + 1;
			dhd_awdl_peer_op(dhd, (uint8)ifidx, ((char *)buf + slen), len - slen);
		}
		/* Intercept the awdl ioctl here, delete flow rings if awdl is
		 * disabled
		 */
		if (!strcmp(buf, "awdl")) {
			int val = 0;
			slen = strlen("awdl") + 1;
			if (len >= (int)(slen + sizeof(int))) {
				bcopy(((char *)buf + slen), &val, sizeof(int));
				val = ltoh32(val);
				if (val == TRUE) {
					/**
					 * Though we are updating the link status when we recieve
					 * WLC_E_LINK from dongle, it is not gaurenteed always.
					 * So intercepting the awdl command fired from app to
					 * update the status.
					 */
					dhd_update_interface_link_status(dhd, (uint8)ifidx, TRUE);
#if defined(DHD_AWDL) && defined(AWDL_SLOT_STATS)
					/* reset AWDL stats data structures when AWDL is enabled */
					dhd_clear_awdl_stats(dhd);
#endif /* DHD_AWDL && AWDL_SLOT_STATS */
				} else if (val == FALSE) {
					dhd_update_interface_link_status(dhd, (uint8)ifidx, FALSE);
					dhd_del_all_sta(dhd, (uint8)ifidx);
					dhd_awdl_peer_op(dhd, (uint8)ifidx, NULL, 0);

				}
			}

		}

		/* store the awdl min extension count and presence mode values
		 * set by the user, same will be inserted in the LLC header for
		 * each tx packet on the awdl iface
		*/
		slen = strlen("awdl_extcounts");
		if (!strncmp(buf, "awdl_extcounts", slen)) {
			awdl_extcount_t *extcnt = NULL;
			slen = slen + 1;
			if ((len - slen) >= sizeof(*extcnt)) {
				extcnt = (awdl_extcount_t *)((char *)buf + slen);
				dhd->awdl_minext = extcnt->minExt;
			}
		}

		slen = strlen("awdl_presencemode");
		if (!strncmp(buf, "awdl_presencemode", slen)) {
			slen = slen + 1;
			if ((len - slen) >= sizeof(uint8)) {
				dhd->awdl_presmode = *((uint8 *)((char *)buf + slen));
			}
		}
#endif /* DHD_AWDL */
	}

}

#ifdef DHD_PM_CONTROL_FROM_FILE
extern bool g_pm_control;
#endif /* DHD_PM_CONTROL_FROM_FILE */

/** Use protocol to issue ioctl to dongle. Only one ioctl may be in transit. */
int dhd_prot_ioctl(dhd_pub_t *dhd, int ifidx, wl_ioctl_t * ioc, void * buf, int len)
{
	int ret = -1;
	uint8 action;

	if (dhd->bus->is_linkdown) {
		DHD_ERROR_RLMT(("%s : PCIe link is down. we have nothing to do\n", __FUNCTION__));
		goto done;
	}

	if (dhd_query_bus_erros(dhd)) {
		DHD_ERROR_RLMT(("%s : some BUS error. we have nothing to do\n", __FUNCTION__));
		goto done;
	}

	if ((dhd->busstate == DHD_BUS_DOWN) || dhd->hang_was_sent) {
		DHD_ERROR_RLMT(("%s : bus is down. we have nothing to do -"
			" bus state: %d, sent hang: %d\n", __FUNCTION__,
			dhd->busstate, dhd->hang_was_sent));
		goto done;
	}

	if (dhd->busstate == DHD_BUS_SUSPEND) {
		DHD_ERROR(("%s : bus is suspended\n", __FUNCTION__));
		goto done;
	}

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef DHD_PCIE_REG_ACCESS
#ifdef BOARD_HIKEY
#ifndef PCIE_LNK_SPEED_GEN1
#define PCIE_LNK_SPEED_GEN1		0x1
#endif
	/* BUG_ON if link speed is GEN1 in Hikey for 4389B0 */
	if (dhd->bus->sih->buscorerev == 72) {
		if (dhd_get_pcie_linkspeed(dhd) == PCIE_LNK_SPEED_GEN1) {
			DHD_ERROR(("%s: ******* Link Speed is GEN1 *********\n", __FUNCTION__));
			BUG_ON(1);
		}
	}
#endif /* BOARD_HIKEY */
#endif /* DHD_PCIE_REG_ACCESS */

	if (ioc->cmd == WLC_SET_PM) {
#ifdef DHD_PM_CONTROL_FROM_FILE
		if (g_pm_control == TRUE) {
			DHD_ERROR(("%s: SET PM ignored!(Requested:%d)\n",
				__FUNCTION__, buf ? *(char *)buf : 0));
			goto done;
		}
#endif /* DHD_PM_CONTROL_FROM_FILE */
#ifdef DHD_PM_OVERRIDE
		{
			extern bool g_pm_override;
			if (g_pm_override == TRUE) {
				DHD_ERROR(("%s: PM override SET PM ignored!(Requested:%d)\n",
					__FUNCTION__, buf ? *(char *)buf : 0));
				goto done;
			}
		}
#endif /* DHD_PM_OVERRIDE */
		DHD_TRACE_HW4(("%s: SET PM to %d\n", __FUNCTION__, buf ? *(char *)buf : 0));
	}

	ASSERT(len <= WLC_IOCTL_MAXLEN);

	if (len > WLC_IOCTL_MAXLEN)
		goto done;

	action = ioc->set;

	dhd_prot_wlioctl_intercept(dhd, ioc, buf);

#if defined(EXT_STA)
	wl_dbglog_ioctl_add(ioc, len, NULL);
#endif
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
#ifndef DETAIL_DEBUG_LOG_FOR_IOCTL
		DHD_INFO(("%s: status ret value is %d \n", __FUNCTION__, ret));
#endif /* !DETAIL_DEBUG_LOG_FOR_IOCTL */
		dhd->dongle_error = ret;
	}

	dhd_prot_wl_ioctl_ret_intercept(dhd, ioc, buf, ifidx, ret, len);

done:
	return ret;

} /* dhd_prot_ioctl */

/** test / loopback */

/*
 * XXX: This will fail with new PCIe Split header Full Dongle using fixed
 * sized messages in control submission ring. We seem to be sending the lpbk
 * data via the control message, wherein the lpbk data may be larger than 1
 * control message that is being committed.
 */
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

	DHD_RING_LOCK(ring->ring_lock, flags);

	ioct_rqst = (ioct_reqst_hdr_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (ioct_rqst == NULL) {
		DHD_RING_UNLOCK(ring->ring_lock, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return 0;
	}

	{
		uint8 *ptr;
		uint16 i;

		ptr = (uint8 *)ioct_rqst; /* XXX: failure!!! */
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

	DHD_RING_UNLOCK(ring->ring_lock, flags);

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

	dhdp->bus->dmaxfer_complete = TRUE;
	dhd_os_dmaxfer_wake(dhdp);
} /* dmaxfer_free_prev_dmaaddr */

/** test / loopback */
int dmaxfer_prepare_dmaaddr(dhd_pub_t *dhd, uint len,
	uint srcdelay, uint destdelay, dhd_dmaxfer_t *dmaxfer)
{
	uint i = 0, j = 0;
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

	/* Populate source with a pattern like below
	 * 0x00000000
	 * 0x01010101
	 * 0x02020202
	 * 0x03030303
	 * 0x04040404
	 * 0x05050505
	 * ...
	 * 0xFFFFFFFF
	 */
	while (i < dmaxfer->len) {
		((uint8*)dmaxfer->srcmem.va)[i] = j % 256;
		i++;
		if (i % 4 == 0) {
			j++;
		}
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
	int buf_free_scheduled;
	int err = 0;

	BCM_REFERENCE(cmplt);
	end_usec = OSL_SYSUPTIME_US();

#if defined(DHD_EFI) && defined(DHD_INTR_POLL_PERIOD_DYNAMIC)
	/* restore interrupt poll period to the previous existing value */
	dhd_os_set_intr_poll_period(dhd->bus, dhd->cur_intr_poll_period);
#endif /* DHD_EFI && DHD_INTR_POLL_PERIOD_DYNAMIC */

	DHD_ERROR(("DMA loopback status: %d\n", cmplt->compl_hdr.status));
	prot->dmaxfer.status = cmplt->compl_hdr.status;
	OSL_CACHE_INV(prot->dmaxfer.dstmem.va, prot->dmaxfer.len);
	if (prot->dmaxfer.d11_lpbk != M2M_WRITE_TO_RAM &&
		prot->dmaxfer.d11_lpbk != M2M_READ_FROM_RAM &&
		prot->dmaxfer.d11_lpbk != D11_WRITE_TO_RAM &&
		prot->dmaxfer.d11_lpbk != D11_READ_FROM_RAM) {
		err = memcmp(prot->dmaxfer.srcmem.va,
			prot->dmaxfer.dstmem.va, prot->dmaxfer.len);
	}
	if (prot->dmaxfer.srcmem.va && prot->dmaxfer.dstmem.va) {
		if (err ||
		        cmplt->compl_hdr.status != BCME_OK) {
		        DHD_ERROR(("DMA loopback failed\n"));
			/* it is observed that some times the completion
			 * header status is set as OK, but the memcmp fails
			 * hence always explicitly set the dmaxfer status
			 * as error if this happens.
			 */
			prot->dmaxfer.status = BCME_ERROR;
			prhex("XFER SRC: ",
			    prot->dmaxfer.srcmem.va, prot->dmaxfer.len);
			prhex("XFER DST: ",
			    prot->dmaxfer.dstmem.va, prot->dmaxfer.len);
		}
		else {
			switch (prot->dmaxfer.d11_lpbk) {
			case M2M_DMA_LPBK: {
				DHD_ERROR(("DMA successful pcie m2m DMA loopback\n"));
				} break;
			case D11_LPBK: {
				DHD_ERROR(("DMA successful with d11 loopback\n"));
				} break;
			case BMC_LPBK: {
				DHD_ERROR(("DMA successful with bmc loopback\n"));
				} break;
			case M2M_NON_DMA_LPBK: {
				DHD_ERROR(("DMA successful pcie m2m NON DMA loopback\n"));
				} break;
			case D11_HOST_MEM_LPBK: {
				DHD_ERROR(("DMA successful d11 host mem loopback\n"));
				} break;
			case BMC_HOST_MEM_LPBK: {
				DHD_ERROR(("DMA successful bmc host mem loopback\n"));
				} break;
			case M2M_WRITE_TO_RAM: {
				DHD_ERROR(("DMA successful pcie m2m write to ram\n"));
				} break;
			case M2M_READ_FROM_RAM: {
				DHD_ERROR(("DMA successful pcie m2m read from ram\n"));
				prhex("XFER DST: ",
					prot->dmaxfer.dstmem.va, prot->dmaxfer.len);
				} break;
			case D11_WRITE_TO_RAM: {
				DHD_ERROR(("DMA successful D11 write to ram\n"));
				} break;
			case D11_READ_FROM_RAM: {
				DHD_ERROR(("DMA successful D11 read from ram\n"));
				prhex("XFER DST: ",
					prot->dmaxfer.dstmem.va, prot->dmaxfer.len);
				} break;
			default: {
				DHD_ERROR(("Invalid loopback option\n"));
				} break;
			}

			if (DHD_LPBKDTDUMP_ON()) {
				/* debug info print of the Tx and Rx buffers */
				dhd_prhex("XFER SRC: ", prot->dmaxfer.srcmem.va,
					prot->dmaxfer.len, DHD_INFO_VAL);
				dhd_prhex("XFER DST: ", prot->dmaxfer.dstmem.va,
					prot->dmaxfer.len, DHD_INFO_VAL);
			}
		}
	}

	buf_free_scheduled = dhd_prepare_schedule_dmaxfer_free(dhd);
	end_usec -= prot->dmaxfer.start_usec;
	if (end_usec) {
		prot->dmaxfer.time_taken = end_usec;
		DHD_ERROR(("DMA loopback %d bytes in %lu usec, %u kBps\n",
			prot->dmaxfer.len, (unsigned long)end_usec,
			(prot->dmaxfer.len * (1000 * 1000 / 1024) / (uint32)end_usec)));
	}
	dhd->prot->dmaxfer.in_progress = FALSE;

	if (buf_free_scheduled != BCME_OK) {
		dhd->bus->dmaxfer_complete = TRUE;
		dhd_os_dmaxfer_wake(dhd);
	}
}

/** Test functionality.
 * Transfers bytes from host to dongle and to host again using DMA
 * This function is not reentrant, as prot->dmaxfer.in_progress is not protected
 * by a spinlock.
 */
int
dhdmsgbuf_dmaxfer_req(dhd_pub_t *dhd, uint len, uint srcdelay, uint destdelay,
	uint d11_lpbk, uint core_num, uint32 mem_addr)
{
	unsigned long flags;
	int ret = BCME_OK;
	dhd_prot_t *prot = dhd->prot;
	pcie_dma_xfer_params_t *dmap;
	uint32 xferlen = LIMIT_TO_MAX(len, DMA_XFER_LEN_LIMIT);
	uint16 alloced = 0;
	msgbuf_ring_t *ring = &prot->h2dring_ctrl_subn;

	/* XXX: prot->dmaxfer.in_progress is not protected by lock */
	if (prot->dmaxfer.in_progress) {
		DHD_ERROR(("DMA is in progress...\n"));
		return BCME_ERROR;
	}

	if (d11_lpbk >= MAX_LPBK) {
		DHD_ERROR(("loopback mode should be either"
			" 0-PCIE_M2M_DMA, 1-D11, 2-BMC or 3-PCIE_M2M_NonDMA\n"));
		return BCME_ERROR;
	}

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK) {
		return BCME_ERROR;
	}
#endif /* PCIE_INB_DW */

	prot->dmaxfer.in_progress = TRUE;
	if ((ret = dmaxfer_prepare_dmaaddr(dhd, xferlen, srcdelay, destdelay,
	        &prot->dmaxfer)) != BCME_OK) {
		prot->dmaxfer.in_progress = FALSE;
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
		return ret;
	}
	DHD_RING_LOCK(ring->ring_lock, flags);
	dmap = (pcie_dma_xfer_params_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (dmap == NULL) {
		dmaxfer_free_dmaaddr(dhd, &prot->dmaxfer);
		prot->dmaxfer.in_progress = FALSE;
		DHD_RING_UNLOCK(ring->ring_lock, flags);
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
	prot->dmaxfer.d11_lpbk = d11_lpbk;
	if (d11_lpbk == M2M_WRITE_TO_RAM) {
		dmap->host_ouput_buf_addr.high = 0x0;
		dmap->host_ouput_buf_addr.low = mem_addr;
	} else if (d11_lpbk == M2M_READ_FROM_RAM) {
		dmap->host_input_buf_addr.high = 0x0;
		dmap->host_input_buf_addr.low = mem_addr;
	} else if (d11_lpbk == D11_WRITE_TO_RAM) {
		dmap->host_ouput_buf_addr.high = 0x0;
		dmap->host_ouput_buf_addr.low = mem_addr;
	} else if (d11_lpbk == D11_READ_FROM_RAM) {
		dmap->host_input_buf_addr.high = 0x0;
		dmap->host_input_buf_addr.low = mem_addr;
	}
	dmap->flags = (((core_num & PCIE_DMA_XFER_FLG_CORE_NUMBER_MASK)
			<< PCIE_DMA_XFER_FLG_CORE_NUMBER_SHIFT) |
			((prot->dmaxfer.d11_lpbk & PCIE_DMA_XFER_FLG_D11_LPBK_MASK)
			 << PCIE_DMA_XFER_FLG_D11_LPBK_SHIFT));
	prot->dmaxfer.start_usec = OSL_SYSUPTIME_US();

	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, dmap, 1);

	DHD_RING_UNLOCK(ring->ring_lock, flags);

	DHD_ERROR(("DMA loopback Started... on core[%d]\n", core_num));
#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return BCME_OK;
} /* dhdmsgbuf_dmaxfer_req */

int
dhdmsgbuf_dmaxfer_status(dhd_pub_t *dhd, dma_xfer_info_t *result)
{
	dhd_prot_t *prot = dhd->prot;

	if (prot->dmaxfer.in_progress)
		result->status = DMA_XFER_IN_PROGRESS;
	else if (prot->dmaxfer.status == 0)
		result->status = DMA_XFER_SUCCESS;
	else
		result->status = DMA_XFER_FAILED;

	result->type = prot->dmaxfer.d11_lpbk;
	result->error_code = prot->dmaxfer.status;
	result->num_bytes = prot->dmaxfer.len;
	result->time_taken = prot->dmaxfer.time_taken;
	if (prot->dmaxfer.time_taken) {
		/* throughput in kBps */
		result->tput =
			(prot->dmaxfer.len * (1000 * 1000 / 1024)) /
			(uint32)prot->dmaxfer.time_taken;
	}

	return BCME_OK;
}

/** Called in the process of submitting an ioctl to the dongle */
static int
dhd_msgbuf_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len, uint8 action)
{
	int ret = 0;
	uint copylen = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("%s : PCIe link is down. we have nothing to do\n",
			__FUNCTION__));
		return -EIO;
	}

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
			strlcpy((char *)buf, bcmerrorstr(dhd->dongle_error), copylen);
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

void
dhd_msgbuf_iovar_timeout_dump(dhd_pub_t *dhd)
{
	uint32 intstatus;
	dhd_prot_t *prot = dhd->prot;
	dhd->rxcnt_timeout++;
	dhd->rx_ctlerrs++;
	DHD_ERROR(("%s: resumed on timeout rxcnt_timeout%s %d ioctl_cmd %d "
		"trans_id %d state %d busstate=%d ioctl_received=%d\n", __FUNCTION__,
		dhd->is_sched_error ? " due to scheduling problem" : "",
		dhd->rxcnt_timeout, prot->curr_ioctl_cmd, prot->ioctl_trans_id,
		prot->ioctl_state, dhd->busstate, prot->ioctl_received));
#if defined(DHD_KERNEL_SCHED_DEBUG) && defined(DHD_FW_COREDUMP)
		/* XXX DHD triggers Kernel panic if the resumed on timeout occurrs
		 * due to tasklet or workqueue scheduling problems in the Linux Kernel.
		 * Customer informs that it is hard to find any clue from the
		 * host memory dump since the important tasklet or workqueue information
		 * is already disappered due the latency while printing out the timestamp
		 * logs for debugging scan timeout issue.
		 * For this reason, customer requestes us to trigger Kernel Panic rather than
		 * taking a SOCRAM dump.
		 */
		if (dhd->is_sched_error && dhd->memdump_enabled == DUMP_MEMFILE_BUGON) {
			/* change g_assert_type to trigger Kernel panic */
			g_assert_type = 2;
			/* use ASSERT() to trigger panic */
			ASSERT(0);
		}
#endif /* DHD_KERNEL_SCHED_DEBUG && DHD_FW_COREDUMP */

	if (prot->curr_ioctl_cmd == WLC_SET_VAR ||
			prot->curr_ioctl_cmd == WLC_GET_VAR) {
		char iovbuf[32];
		int dump_size = 128;
		uint8 *ioctl_buf = (uint8 *)prot->ioctbuf.va;
		memset(iovbuf, 0, sizeof(iovbuf));
		strncpy(iovbuf, ioctl_buf, sizeof(iovbuf) - 1);
		iovbuf[sizeof(iovbuf) - 1] = '\0';
		DHD_ERROR(("Current IOVAR (%s): %s\n",
			prot->curr_ioctl_cmd == WLC_SET_VAR ?
			"WLC_SET_VAR" : "WLC_GET_VAR", iovbuf));
		DHD_ERROR(("========== START IOCTL REQBUF DUMP ==========\n"));
		prhex("ioctl_buf", (const u8 *) ioctl_buf, dump_size);
		DHD_ERROR(("\n========== END IOCTL REQBUF DUMP ==========\n"));
	}

	/* Check the PCIe link status by reading intstatus register */
	intstatus = si_corereg(dhd->bus->sih,
		dhd->bus->sih->buscoreidx, dhd->bus->pcie_mailbox_int, 0, 0);
	if (intstatus == (uint32)-1) {
		DHD_ERROR(("%s : PCIe link might be down\n", __FUNCTION__));
		dhd->bus->is_linkdown = TRUE;
	}

	dhd_bus_dump_console_buffer(dhd->bus);
	dhd_prot_debug_info_print(dhd);
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
	static uint cnt = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhd_query_bus_erros(dhd)) {
		ret = -EIO;
		goto out;
	}
#ifdef GDB_PROXY
	/* Loop while timeout is caused by firmware stop in GDB */
	{
		uint32 prev_stop_count;
		do {
			prev_stop_count = dhd->gdb_proxy_stop_count;
			timeleft = dhd_os_ioctl_resp_wait(dhd, (uint *)&prot->ioctl_received);
		} while ((timeleft == 0) && ((dhd->gdb_proxy_stop_count != prev_stop_count) ||
			(dhd->gdb_proxy_stop_count & GDB_PROXY_STOP_MASK)));
	}
#else
	timeleft = dhd_os_ioctl_resp_wait(dhd, (uint *)&prot->ioctl_received);
#endif /* GDB_PROXY */

#ifdef DHD_RECOVER_TIMEOUT
	if (prot->ioctl_received == 0) {
		uint32 intstatus = si_corereg(dhd->bus->sih,
			dhd->bus->sih->buscoreidx, dhd->bus->pcie_mailbox_int, 0, 0);
		int host_irq_disbled = dhdpcie_irq_disabled(dhd->bus);
		if ((intstatus) && (intstatus != (uint32)-1) &&
			(timeleft == 0) && (!dhd_query_bus_erros(dhd))) {
			DHD_ERROR(("%s: iovar timeout trying again intstatus=%x"
				" host_irq_disabled=%d\n",
				__FUNCTION__, intstatus, host_irq_disbled));
			dhd_pcie_intr_count_dump(dhd);
			dhd_print_tasklet_status(dhd);
			dhd_prot_process_ctrlbuf(dhd);
			timeleft = dhd_os_ioctl_resp_wait(dhd, (uint *)&prot->ioctl_received);
			/* Clear Interrupts */
			dhdpcie_bus_clear_intstatus(dhd->bus);
		}
	}
#endif /* DHD_RECOVER_TIMEOUT */

	if (dhd->conf->ctrl_resched > 0 && timeleft == 0 && (!dhd_query_bus_erros(dhd))) {
		cnt++;
		if (cnt <= dhd->conf->ctrl_resched) {
			uint buscorerev = dhd->bus->sih->buscorerev;
			uint32 intstatus = 0, intmask = 0;
			intstatus = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, PCIMailBoxInt(buscorerev), 0, 0);
			intmask = si_corereg(dhd->bus->sih, dhd->bus->sih->buscoreidx, PCIMailBoxMask(buscorerev), 0, 0);
			if (intstatus) {
				DHD_ERROR(("%s: reschedule dhd_dpc, cnt=%d, intstatus=0x%x, intmask=0x%x\n",
					__FUNCTION__, cnt, intstatus, intmask));
				dhd->bus->intstatus = intstatus;
				dhd->bus->ipend = TRUE;
				dhd->bus->dpc_sched = TRUE;
				dhd_sched_dpc(dhd);
				timeleft = dhd_os_ioctl_resp_wait(dhd, &prot->ioctl_received);
			}
		}
	} else {
		cnt = 0;
	}

	if (timeleft == 0 && (!dhd_query_bus_erros(dhd))) {
		if (dhd->check_trap_rot) {
			/* check dongle trap first */
			DHD_ERROR(("Check dongle trap in the case of iovar timeout\n"));
			dhd_bus_checkdied(dhd->bus, NULL, 0);

			if (dhd->dongle_trap_occured) {
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
				dhd->bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */
				ret = -EREMOTEIO;
				goto out;
			}
		}
		/* check if resumed on time out related to scheduling issue */
		dhd->is_sched_error = dhd_bus_query_dpc_sched_errors(dhd);

		dhd->iovar_timeout_occured = TRUE;
		dhd_msgbuf_iovar_timeout_dump(dhd);

#ifdef DHD_FW_COREDUMP
		/* Collect socram dump */
		if (dhd->memdump_enabled) {
			/* collect core dump */
			dhd->memdump_type = DUMP_TYPE_RESUMED_ON_TIMEOUT;
			dhd_bus_mem_dump(dhd);
		}
#endif /* DHD_FW_COREDUMP */

#ifdef DHD_EFI
		/*
		* for ioctl timeout, recovery is triggered only for EFI case, because
		* in linux, dhd daemon will itself trap the FW,
		* so if recovery is triggered
		* then there is a race between FLR and daemon initiated trap
		*/
		dhd_schedule_reset(dhd);
#endif /* DHD_EFI */

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

	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("%s : PCIe link is down. we have nothing to do\n",
			__FUNCTION__));
		return -EIO;
	}

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

#ifdef DHD_DUMP_PCIE_RINGS
int dhd_d2h_h2d_ring_dump(dhd_pub_t *dhd, void *file, const void *user_buf,
	unsigned long *file_posn, bool file_write)
{
	dhd_prot_t *prot;
	msgbuf_ring_t *ring;
	int ret = 0;
	uint16 h2d_flowrings_total;
	uint16 flowid;

	if (!(dhd) || !(dhd->prot)) {
		goto exit;
	}
	prot = dhd->prot;

	/* Below is the same ring dump sequence followed in parser as well. */
	ring = &prot->h2dring_ctrl_subn;
	if ((ret = dhd_ring_write(dhd, ring, file, user_buf, file_posn)) < 0)
		goto exit;

	ring = &prot->h2dring_rxp_subn;
	if ((ret = dhd_ring_write(dhd, ring, file, user_buf, file_posn)) < 0)
		goto exit;

	ring = &prot->d2hring_ctrl_cpln;
	if ((ret = dhd_ring_write(dhd, ring, file, user_buf, file_posn)) < 0)
		goto exit;

	ring = &prot->d2hring_tx_cpln;
	if ((ret = dhd_ring_write(dhd, ring, file, user_buf, file_posn)) < 0)
		goto exit;

	ring = &prot->d2hring_rx_cpln;
	if ((ret = dhd_ring_write(dhd, ring, file, user_buf, file_posn)) < 0)
		goto exit;

	h2d_flowrings_total = dhd_get_max_flow_rings(dhd);
	FOREACH_RING_IN_FLOWRINGS_POOL(prot, ring, flowid, h2d_flowrings_total) {
		if ((ret = dhd_ring_write(dhd, ring, file, user_buf, file_posn)) < 0) {
			goto exit;
		}
	}

#ifdef EWP_EDL
	if (dhd->dongle_edl_support) {
		ring = prot->d2hring_edl;
		if ((ret = dhd_edl_ring_hdr_write(dhd, ring, file, user_buf, file_posn)) < 0)
			goto exit;
	}
	else if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_6 && !dhd->dongle_edl_support)
#else
	if (dhd->bus->api.fw_rev >= PCIE_SHARED_VERSION_6)
#endif /* EWP_EDL */
	{
		ring = prot->h2dring_info_subn;
		if ((ret = dhd_ring_write(dhd, ring, file, user_buf, file_posn)) < 0)
			goto exit;

		ring = prot->d2hring_info_cpln;
		if ((ret = dhd_ring_write(dhd, ring, file, user_buf, file_posn)) < 0)
			goto exit;
	}

exit :
	return ret;
}

/* Write to file */
static
int dhd_ring_write(dhd_pub_t *dhd, msgbuf_ring_t *ring, void *file,
	const void *user_buf, unsigned long *file_posn)
{
	int ret = 0;

	if (ring == NULL) {
		DHD_ERROR(("%s: Ring not initialised, failed to dump ring contents\n",
			__FUNCTION__));
		return BCME_ERROR;
	}
	if (file) {
		ret = dhd_os_write_file_posn(file, file_posn, (char *)(ring->dma_buf.va),
				((unsigned long)(ring->max_items) * (ring->item_len)));
		if (ret < 0) {
			DHD_ERROR(("%s: write file error !\n", __FUNCTION__));
			ret = BCME_ERROR;
		}
	} else if (user_buf) {
		ret = dhd_export_debug_data((char *)(ring->dma_buf.va), NULL, user_buf,
			((unsigned long)(ring->max_items) * (ring->item_len)), (int *)file_posn);
	}
	return ret;
}

#ifdef EWP_EDL
/* Write to file */
static
int dhd_edl_ring_hdr_write(dhd_pub_t *dhd, msgbuf_ring_t *ring, void *file, const void *user_buf,
	unsigned long *file_posn)
{
	int ret = 0, nitems = 0;
	char *buf = NULL, *ptr = NULL;
	uint8 *msg_addr = NULL;
	uint16	rd = 0;

	if (ring == NULL) {
		DHD_ERROR(("%s: Ring not initialised, failed to dump ring contents\n",
			__FUNCTION__));
		ret = BCME_ERROR;
		goto done;
	}

	buf = MALLOCZ(dhd->osh, (D2HRING_EDL_MAX_ITEM * D2HRING_EDL_HDR_SIZE));
	if (buf == NULL) {
		DHD_ERROR(("%s: buffer allocation failed\n", __FUNCTION__));
		ret = BCME_ERROR;
		goto done;
	}
	ptr = buf;

	for (; nitems < D2HRING_EDL_MAX_ITEM; nitems++, rd++) {
		msg_addr = (uint8 *)ring->dma_buf.va + (rd * ring->item_len);
		memcpy(ptr, (char *)msg_addr, D2HRING_EDL_HDR_SIZE);
		ptr += D2HRING_EDL_HDR_SIZE;
	}
	if (file) {
		ret = dhd_os_write_file_posn(file, file_posn, buf,
				(D2HRING_EDL_HDR_SIZE * D2HRING_EDL_MAX_ITEM));
		if (ret < 0) {
			DHD_ERROR(("%s: write file error !\n", __FUNCTION__));
			goto done;
		}
	}
	else {
		ret = dhd_export_debug_data(buf, NULL, user_buf,
			(D2HRING_EDL_HDR_SIZE * D2HRING_EDL_MAX_ITEM), file_posn);
	}

done:
	if (buf) {
		MFREE(dhd->osh, buf, (D2HRING_EDL_MAX_ITEM * D2HRING_EDL_HDR_SIZE));
	}
	return ret;
}
#endif /* EWP_EDL */
#endif /* DHD_DUMP_PCIE_RINGS */

/** Add prot dump output to a buffer */
void dhd_prot_dump(dhd_pub_t *dhd, struct bcmstrbuf *b)
{
#if defined(BCM_ROUTER_DHD)
	bcm_bprintf(b, "DHD Router: 1GMAC HotBRC forwarding mode\n");
#endif /* BCM_ROUTER_DHD */

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
#if defined(DHD_HTPUT_TUNABLES)
	bcm_bprintf(b, "h2d_htput_max_txpost: %d, prot->h2d_htput_max_txpost: %d\n",
		h2d_htput_max_txpost, dhd->prot->h2d_htput_max_txpost);
#endif /* DHD_HTPUT_TUNABLES */
	bcm_bprintf(b, "pktid_txq_start_cnt: %d\n", dhd->prot->pktid_txq_start_cnt);
	bcm_bprintf(b, "pktid_txq_stop_cnt: %d\n", dhd->prot->pktid_txq_stop_cnt);
	bcm_bprintf(b, "pktid_depleted_cnt: %d\n", dhd->prot->pktid_depleted_cnt);
	bcm_bprintf(b, "txcpl_db_cnt: %d\n", dhd->prot->txcpl_db_cnt);
#ifdef DHD_DMA_INDICES_SEQNUM
	bcm_bprintf(b, "host_seqnum %u dngl_seqnum %u\n", dhd_prot_read_seqnum(dhd, TRUE),
		dhd_prot_read_seqnum(dhd, FALSE));
#endif /* DHD_DMA_INDICES_SEQNUM */
	bcm_bprintf(b, "tx_h2d_db_cnt:%llu\n", dhd->prot->tx_h2d_db_cnt);
#ifdef AGG_H2D_DB
	bcm_bprintf(b, "agg_h2d_db_enab:%d agg_h2d_db_timeout:%d agg_h2d_db_inflight_thresh:%d\n",
		agg_h2d_db_enab, agg_h2d_db_timeout, agg_h2d_db_inflight_thresh);
	bcm_bprintf(b, "agg_h2d_db: timer_db_cnt:%d direct_db_cnt:%d\n",
		dhd->prot->agg_h2d_db_info.timer_db_cnt, dhd->prot->agg_h2d_db_info.direct_db_cnt);
	dhd_agg_inflight_stats_dump(dhd, b);
#endif /* AGG_H2D_DB */
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
	DHD_RING_LOCK(ring->ring_lock, flags);

	hevent = (hostevent_hdr_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (hevent == NULL) {
		DHD_RING_UNLOCK(ring->ring_lock, flags);
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

	DHD_RING_UNLOCK(ring->ring_lock, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return 0;
}

/**
 * If exactly_nitems is true, this function will allocate space for nitems or fail
 * If exactly_nitems is false, this function will allocate space for nitems or less
 */
static void *
BCMFASTPATH(dhd_prot_alloc_ring_space)(dhd_pub_t *dhd, msgbuf_ring_t *ring,
	uint16 nitems, uint16 * alloced, bool exactly_nitems)
{
	void * ret_buf;

	if (nitems == 0) {
		DHD_ERROR(("%s: nitems is 0 - ring(%s)\n", __FUNCTION__, ring->name));
		return NULL;
	}

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
				DHD_ERROR(("%s: Invalid rd idx=%d\n", ring->name, ring->rd));
				dhd->bus->read_shm_fail = TRUE;
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
		DHD_MSGBUF_INFO(("%s: setting the phase now\n", ring->name));
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
#ifdef DBG_DW_CHK_PCIE_READ_LATENCY
	ulong addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
	ktime_t begin_time, end_time;
	s64 diff_ns;
#endif /* DBG_DW_CHK_PCIE_READ_LATENCY */

	if (dhd_query_bus_erros(dhd)) {
		return -EIO;
	}

	rqstlen = len;
	resplen = len;

	/* Limit ioct request to MSGBUF_MAX_MSG_SIZE bytes including hdrs */
	/* 8K allocation of dongle buffer fails */
	/* dhd doesnt give separate input & output buf lens */
	/* so making the assumption that input length can never be more than 2k */
	rqstlen = MIN(rqstlen, MSGBUF_IOCTL_MAX_RQSTLEN);

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;

#ifdef DBG_DW_CHK_PCIE_READ_LATENCY
	preempt_disable();
	begin_time = ktime_get();
	R_REG(dhd->osh, (volatile uint16 *)(dhd->bus->tcm + addr));
	end_time = ktime_get();
	preempt_enable();
	diff_ns = ktime_to_ns(ktime_sub(end_time, begin_time));
	/* Check if the delta is greater than 1 msec */
	if (diff_ns > (1 * NSEC_PER_MSEC)) {
		DHD_ERROR(("%s: found latency over 1ms (%lld ns), ds state=%d\n", __func__,
		       diff_ns, dhdpcie_bus_get_pcie_inband_dw_state(dhd->bus)));
	}
#endif /* DBG_DW_CHK_PCIE_READ_LATENCY */
#endif /* PCIE_INB_DW */

	DHD_RING_LOCK(ring->ring_lock, flags);

	if (prot->ioctl_state) {
		DHD_ERROR(("%s: pending ioctl %02x\n", __FUNCTION__, prot->ioctl_state));
		DHD_RING_UNLOCK(ring->ring_lock, flags);
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
		DHD_RING_UNLOCK(ring->ring_lock, flags);
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

	prot->ioctl_fillup_time = OSL_LOCALTIME_NS();

	if (buf)
		memcpy(ioct_buf, buf, len);

	OSL_CACHE_FLUSH((void *) prot->ioctbuf.va, len);

	if (!ISALIGNED(ioct_buf, DMA_ALIGN_LEN))
		DHD_ERROR(("host ioct address unaligned !!!!! \n"));

	DHD_CTL(("submitted IOCTL request request_id %d, cmd %d, output_buf_len %d, tx_id %d\n",
		ioct_rqst->cmn_hdr.request_id, cmd, ioct_rqst->output_buf_len,
		ioct_rqst->trans_id));

#if defined(BCMINTERNAL) && defined(DHD_DBG_DUMP)
	dhd_prot_ioctl_trace(dhd, ioct_rqst, buf, len);
#endif /* defined(BCMINTERNAL) && defined(DHD_DBG_DUMP) */

	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, ioct_rqst, 1);

	DHD_RING_UNLOCK(ring->ring_lock, flags);

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
	uint32 dma_buf_len;
	dhd_prot_t *prot = dhd->prot;
	uint16 max_flowrings = dhd->bus->max_tx_flowrings;
	dhd_dma_buf_t *dma_buf = NULL;

	ASSERT(ring);
	ASSERT(name);
	ASSERT((max_items < 0xFFFF) && (item_len < 0xFFFF) && (ringid < 0xFFFF));

	/* Init name */
	strlcpy((char *)ring->name, name, sizeof(ring->name));

	ring->idx = ringid;

#if defined(DHD_HTPUT_TUNABLES)
	/* Use HTPUT max items */
	if (DHD_IS_FLOWRING(ringid, max_flowrings) &&
		DHD_IS_FLOWID_HTPUT(dhd, DHD_RINGID_TO_FLOWID(ringid))) {
		max_items = prot->h2d_htput_max_txpost;
	}
#endif /* DHD_HTPUT_TUNABLES */

	dma_buf_len = max_items * item_len;

	ring->max_items = max_items;
	ring->item_len = item_len;

	/* A contiguous space may be reserved for all flowrings */
	if (DHD_IS_FLOWRING(ringid, max_flowrings) && (prot->flowrings_dma_buf.va)) {
		/* Carve out from the contiguous DMA-able flowring buffer */
		uint16 flowid;
		uint32 base_offset;
		dhd_dma_buf_t *rsv_buf = &prot->flowrings_dma_buf;

		dma_buf = &ring->dma_buf;

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
#ifdef EWP_EDL
		if (ring == dhd->prot->d2hring_edl) {
			/* For EDL ring, memory is alloced during attach,
			* so just need to copy the dma_buf to the ring's dma_buf
			*/
			memcpy(&ring->dma_buf, &dhd->edl_ring_mem, sizeof(ring->dma_buf));
			dma_buf = &ring->dma_buf;
			if (dma_buf->va == NULL) {
				return BCME_NOMEM;
			}
		} else
#endif /* EWP_EDL */
		{
			/* Allocate a dhd_dma_buf */
			dma_buf_alloced = dhd_dma_buf_alloc(dhd, &ring->dma_buf, dma_buf_len);
			if (dma_buf_alloced != BCME_OK) {
				return BCME_NOMEM;
			}
		}
	}

	/* CAUTION: Save ring::base_addr in little endian format! */
	dhd_base_addr_htolpa(&ring->base_addr, ring->dma_buf.pa);

	ring->ring_lock = osl_spin_lock_init(dhd->osh);

	DHD_INFO(("RING_ATTACH : %s Max item %d len item %d total size %d "
		"ring start %p buf phys addr  %x:%x \n",
		ring->name, ring->max_items, ring->item_len,
		dma_buf_len, ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
		ltoh32(ring->base_addr.low_addr)));

	return BCME_OK;
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

	/* If the DMA-able buffer was carved out of a pre-reserved contiguous
	 * memory, then simply stop using it.
	 */
	if (DHD_IS_FLOWRING(ring->idx, max_flowrings) && (prot->flowrings_dma_buf.va)) {
		(void)dhd_dma_buf_audit(dhd, &ring->dma_buf);
		memset(&ring->dma_buf, 0, sizeof(dhd_dma_buf_t));
	} else {
#ifdef EWP_EDL
		if (ring == dhd->prot->d2hring_edl) {
			/* For EDL ring, do not free ring mem here,
			* it is done in dhd_detach
			*/
			memset(&ring->dma_buf, 0, sizeof(ring->dma_buf));
		} else
#endif /* EWP_EDL */
		{
			dhd_dma_buf_free(dhd, &ring->dma_buf);
		}
	}

	osl_spin_lock_deinit(dhd->osh, ring->ring_lock);

} /* dhd_prot_ring_detach */

/* Fetch number of H2D flowrings given the total number of h2d rings */
uint16
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
		/* For HTPUT case max_items will be changed inside dhd_prot_ring_attach */
		if (dhd_prot_ring_attach(dhd, ring, ring_name,
		        prot->h2d_max_txpost, H2DRING_TXPOST_ITEMSIZE,
		        DHD_FLOWID_TO_RINGID(flowid)) != BCME_OK) {
			goto attach_fail;
		}
	}

	return BCME_OK;

attach_fail:
	/* XXX: On a per project basis, one may decide whether to continue with
	 * "fewer" flowrings, and what value of fewer suffices.
	 */
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

#ifdef AGG_H2D_DB
void
dhd_prot_schedule_aggregate_h2d_db(dhd_pub_t *dhd, uint16 flowid)
{
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring;
	uint16 inflight;
	bool db_req = FALSE;
	bool flush;

	ring = DHD_RING_IN_FLOWRINGS_POOL(prot, flowid);
	flush = !!ring->pend_items_count;
	dhd_prot_txdata_aggr_db_write_flush(dhd, flowid);

	inflight = OSL_ATOMIC_READ(dhd->osh, &ring->inflight);
	if (flush && inflight) {
		if (inflight <= agg_h2d_db_inflight_thresh) {
			db_req = TRUE;
		}
		dhd_agg_inflights_stats_update(dhd, inflight);
		dhd_prot_aggregate_db_ring_door_bell(dhd, flowid, db_req);
	}
}
#endif /* AGG_H2D_DB */

/* Assumes only one index is updated at a time */
/* FIXME Need to fix it */
/* If exactly_nitems is true, this function will allocate space for nitems or fail */
/*    Exception: when wrap around is encountered, to prevent hangup (last nitems of ring buffer) */
/* If exactly_nitems is false, this function will allocate space for nitems or less */
static void *
BCMFASTPATH(dhd_prot_get_ring_space)(msgbuf_ring_t *ring, uint16 nitems, uint16 * alloced,
	bool exactly_nitems)
{
	void *ret_ptr = NULL;
	uint16 ring_avail_cnt;

	ASSERT(nitems <= ring->max_items);

	ring_avail_cnt = CHECK_WRITE_SPACE(ring->rd, ring->wr, ring->max_items);

	if ((ring_avail_cnt == 0) ||
	       (exactly_nitems && (ring_avail_cnt < nitems) &&
	       ((ring->max_items - ring->wr) >= nitems))) {
		DHD_MSGBUF_INFO(("Space not available: ring %s items %d write %d read %d\n",
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

#ifdef AGG_H2D_DB

static void
dhd_prot_agg_db_ring_write(dhd_pub_t *dhd, msgbuf_ring_t * ring, void* p,
		uint16 nitems)
{
	uint16 max_flowrings = dhd->bus->max_tx_flowrings;
	unsigned long flags_bus;

#ifdef DHD_FAKE_TX_STATUS
	/* if fake tx status is enabled, we should not update
	 * dongle side rd/wr index for the tx flowring
	 * and also should not ring the doorbell
	 */
	if (DHD_IS_FLOWRING(ring->idx, max_flowrings)) {
		return;
	}
#endif /* DHD_FAKE_TX_STATUS */

	DHD_BUS_LP_STATE_LOCK(dhd->bus->bus_lp_state_lock, flags_bus);

	/* cache flush */
	OSL_CACHE_FLUSH(p, ring->item_len * nitems);

	if (IDMA_ACTIVE(dhd) || dhd->dma_h2d_ring_upd_support) {
			dhd_prot_dma_indx_set(dhd, ring->wr,
			                      H2D_DMA_INDX_WR_UPD, ring->idx);
	} else if (IFRM_ACTIVE(dhd) && DHD_IS_FLOWRING(ring->idx, max_flowrings)) {
			dhd_prot_dma_indx_set(dhd, ring->wr,
			H2D_IFRM_INDX_WR_UPD, ring->idx);
	} else {
			dhd_bus_cmn_writeshared(dhd->bus, &(ring->wr),
				sizeof(uint16), RING_WR_UPD, ring->idx);
	}

	DHD_BUS_LP_STATE_UNLOCK(dhd->bus->bus_lp_state_lock, flags_bus);
}

static void
dhd_prot_aggregate_db_ring_door_bell(dhd_pub_t *dhd, uint16 flowid, bool ring_db)
{
	dhd_prot_t *prot = dhd->prot;
	flow_ring_table_t *flow_ring_table = (flow_ring_table_t *)dhd->flow_ring_table;
	flow_ring_node_t *flow_ring_node = (flow_ring_node_t *)&flow_ring_table[flowid];
	msgbuf_ring_t *ring = (msgbuf_ring_t *)flow_ring_node->prot_info;
	uint32 db_index;
	uint corerev;

	if (ring_db == TRUE) {
		dhd_msgbuf_agg_h2d_db_timer_cancel(dhd);
		prot->agg_h2d_db_info.direct_db_cnt++;
		/* raise h2d interrupt */
		if (IDMA_ACTIVE(dhd) || (IFRM_ACTIVE(dhd))) {
			db_index = IDMA_IDX0;
			/* this api is called in wl down path..in that case sih is freed already */
			if (dhd->bus->sih) {
				corerev = dhd->bus->sih->buscorerev;
				/* We need to explictly configure the type of DMA for
				 * core rev >= 24
				 */
				if (corerev >= 24) {
					db_index |= (DMA_TYPE_IDMA << DMA_TYPE_SHIFT);
				}
			}
			prot->mb_2_ring_fn(dhd->bus, db_index, TRUE);
		} else {
			prot->mb_ring_fn(dhd->bus, DHD_WRPTR_UPDATE_H2D_DB_MAGIC(ring));
		}
	} else {
		dhd_msgbuf_agg_h2d_db_timer_start(prot);
	}
}

#endif /* AGG_H2D_DB */

/**
 * dhd_prot_ring_write_complete - Host updates the new WR index on producing
 * new messages in a H2D ring. The messages are flushed from cache prior to
 * posting the new WR index. The new WR index will be updated in the DMA index
 * array or directly in the dongle's ring state memory.
 * A PCIE doorbell will be generated to wake up the dongle.
 * This is a non-atomic function, make sure the callers
 * always hold appropriate locks.
 */
static void
BCMFASTPATH(__dhd_prot_ring_write_complete)(dhd_pub_t *dhd, msgbuf_ring_t * ring, void* p,
	uint16 nitems)
{
	dhd_prot_t *prot = dhd->prot;
	uint32 db_index;
	uint16 max_flowrings = dhd->bus->max_tx_flowrings;
	uint corerev;

	/* cache flush */
	OSL_CACHE_FLUSH(p, ring->item_len * nitems);

	if (IDMA_ACTIVE(dhd) || dhd->dma_h2d_ring_upd_support) {
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
		db_index = IDMA_IDX0;
		/* this api is called in wl down path..in that case sih is freed already */
		if (dhd->bus->sih) {
			corerev = dhd->bus->sih->buscorerev;
			/* We need to explictly configure the type of DMA for core rev >= 24 */
			if (corerev >= 24) {
				db_index |= (DMA_TYPE_IDMA << DMA_TYPE_SHIFT);
			}
		}
		prot->mb_2_ring_fn(dhd->bus, db_index, TRUE);
	} else {
		prot->mb_ring_fn(dhd->bus, DHD_WRPTR_UPDATE_H2D_DB_MAGIC(ring));
	}
}

static void
BCMFASTPATH(dhd_prot_ring_write_complete)(dhd_pub_t *dhd, msgbuf_ring_t * ring, void* p,
	uint16 nitems)
{
	unsigned long flags_bus;
	DHD_BUS_LP_STATE_LOCK(dhd->bus->bus_lp_state_lock, flags_bus);
	__dhd_prot_ring_write_complete(dhd, ring, p, nitems);
	DHD_BUS_LP_STATE_UNLOCK(dhd->bus->bus_lp_state_lock, flags_bus);
}

static void
BCMFASTPATH(dhd_prot_ring_doorbell)(dhd_pub_t *dhd, uint32 value)
{
	unsigned long flags_bus;
	DHD_BUS_LP_STATE_LOCK(dhd->bus->bus_lp_state_lock, flags_bus);
	dhd->prot->mb_ring_fn(dhd->bus, value);
	DHD_BUS_LP_STATE_UNLOCK(dhd->bus->bus_lp_state_lock, flags_bus);
}

/**
 * dhd_prot_ring_write_complete_mbdata - will be called from dhd_prot_h2d_mbdata_send_ctrlmsg,
 * which will hold DHD_BUS_LP_STATE_LOCK to update WR pointer, Ring DB and also update
 * bus_low_power_state to indicate D3_INFORM sent in the same BUS_LP_STATE_LOCK.
 */
static void
BCMFASTPATH(dhd_prot_ring_write_complete_mbdata)(dhd_pub_t *dhd, msgbuf_ring_t * ring, void *p,
	uint16 nitems, uint32 mb_data)
{
	unsigned long flags_bus;

	DHD_BUS_LP_STATE_LOCK(dhd->bus->bus_lp_state_lock, flags_bus);

	__dhd_prot_ring_write_complete(dhd, ring, p, nitems);

	/* Mark D3_INFORM in the same context to skip ringing H2D DB after D3_INFORM */
	if (mb_data == H2D_HOST_D3_INFORM) {
		__DHD_SET_BUS_LPS_D3_INFORMED(dhd->bus);
	}

	DHD_BUS_LP_STATE_UNLOCK(dhd->bus->bus_lp_state_lock, flags_bus);
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
	uint32 db_index;
	uint corerev;

	/* update read index */
	/* If dma'ing h2d indices supported
	 * update the r -indices in the
	 * host memory o/w in TCM
	 */
	if (IDMA_ACTIVE(dhd)) {
		dhd_prot_dma_indx_set(dhd, ring->rd,
			D2H_DMA_INDX_RD_UPD, ring->idx);
		db_index = IDMA_IDX1;
		if (dhd->bus->sih) {
			corerev = dhd->bus->sih->buscorerev;
			/* We need to explictly configure the type of DMA for core rev >= 24 */
			if (corerev >= 24) {
				db_index |= (DMA_TYPE_IDMA << DMA_TYPE_SHIFT);
			}
		}
		prot->mb_2_ring_fn(dhd->bus, db_index, FALSE);
	} else if (dhd->dma_h2d_ring_upd_support) {
		dhd_prot_dma_indx_set(dhd, ring->rd,
		                      D2H_DMA_INDX_RD_UPD, ring->idx);
	} else {
		dhd_bus_cmn_writeshared(dhd->bus, &(ring->rd),
			sizeof(uint16), RING_RD_UPD, ring->idx);
	}
}

static int
dhd_send_d2h_ringcreate(dhd_pub_t *dhd, msgbuf_ring_t *ring_to_create,
	uint16 ring_type, uint32 req_id)
{
	unsigned long flags;
	d2h_ring_create_req_t  *d2h_ring;
	uint16 alloced = 0;
	int ret = BCME_OK;
	uint16 max_h2d_rings = dhd->bus->max_submission_rings;
	msgbuf_ring_t *ctrl_ring = &dhd->prot->h2dring_ctrl_subn;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */
	DHD_RING_LOCK(ctrl_ring->ring_lock, flags);

	DHD_TRACE(("%s trying to send D2H ring create Req\n", __FUNCTION__));

	if (ring_to_create == NULL) {
		DHD_ERROR(("%s: FATAL: ring_to_create is NULL\n", __FUNCTION__));
		ret = BCME_ERROR;
		goto err;
	}

	/* Request for ring buffer space */
	d2h_ring = (d2h_ring_create_req_t *) dhd_prot_alloc_ring_space(dhd,
		ctrl_ring, DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D,
		&alloced, FALSE);

	if (d2h_ring == NULL) {
		DHD_ERROR(("%s: FATAL: No space in control ring to send D2H ring create\n",
			__FUNCTION__));
		ret = BCME_NOMEM;
		goto err;
	}
	ring_to_create->create_req_id = (uint16)req_id;
	ring_to_create->create_pending = TRUE;

	/* Common msg buf hdr */
	d2h_ring->msg.msg_type = MSG_TYPE_D2H_RING_CREATE;
	d2h_ring->msg.if_id = 0;
	d2h_ring->msg.flags = ctrl_ring->current_phase;
	d2h_ring->msg.request_id = htol32(ring_to_create->create_req_id);
	d2h_ring->ring_id = htol16(DHD_D2H_RING_OFFSET(ring_to_create->idx, max_h2d_rings));
	DHD_ERROR(("%s ringid: %d idx: %d max_h2d: %d\n", __FUNCTION__, d2h_ring->ring_id,
			ring_to_create->idx, max_h2d_rings));

	d2h_ring->ring_type = ring_type;
	d2h_ring->max_items = htol16(ring_to_create->max_items);
	d2h_ring->len_item = htol16(ring_to_create->item_len);
	d2h_ring->ring_ptr.low_addr = ring_to_create->base_addr.low_addr;
	d2h_ring->ring_ptr.high_addr = ring_to_create->base_addr.high_addr;

	d2h_ring->flags = 0;
	d2h_ring->msg.epoch =
		ctrl_ring->seqnum % H2D_EPOCH_MODULO;
	ctrl_ring->seqnum++;

#ifdef EWP_EDL
	if (ring_type == BCMPCIE_D2H_RING_TYPE_EDL) {
		DHD_ERROR(("%s: sending d2h EDL ring create: "
			"\n max items=%u; len_item=%u; ring_id=%u; low_addr=0x%x; high_addr=0x%x\n",
			__FUNCTION__, ltoh16(d2h_ring->max_items),
			ltoh16(d2h_ring->len_item),
			ltoh16(d2h_ring->ring_id),
			d2h_ring->ring_ptr.low_addr,
			d2h_ring->ring_ptr.high_addr));
	}
#endif /* EWP_EDL */

	/* Update the flow_ring's WRITE index */
	dhd_prot_ring_write_complete(dhd, ctrl_ring, d2h_ring,
		DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D);

	DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return ret;
err:
	DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return ret;
}

static int
dhd_send_h2d_ringcreate(dhd_pub_t *dhd, msgbuf_ring_t *ring_to_create, uint8 ring_type, uint32 id)
{
	unsigned long flags;
	h2d_ring_create_req_t  *h2d_ring;
	uint16 alloced = 0;
	uint8 i = 0;
	int ret = BCME_OK;
	msgbuf_ring_t *ctrl_ring = &dhd->prot->h2dring_ctrl_subn;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhd->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */
	DHD_RING_LOCK(ctrl_ring->ring_lock, flags);

	DHD_TRACE(("%s trying to send H2D ring create Req\n", __FUNCTION__));

	if (ring_to_create == NULL) {
		DHD_ERROR(("%s: FATAL: ring_to_create is NULL\n", __FUNCTION__));
		ret = BCME_ERROR;
		goto err;
	}

	/* Request for ring buffer space */
	h2d_ring = (h2d_ring_create_req_t *)dhd_prot_alloc_ring_space(dhd,
		ctrl_ring, DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D,
		&alloced, FALSE);

	if (h2d_ring == NULL) {
		DHD_ERROR(("%s: FATAL: No space in control ring to send H2D ring create\n",
			__FUNCTION__));
		ret = BCME_NOMEM;
		goto err;
	}
	ring_to_create->create_req_id = (uint16)id;
	ring_to_create->create_pending = TRUE;

	/* Common msg buf hdr */
	h2d_ring->msg.msg_type = MSG_TYPE_H2D_RING_CREATE;
	h2d_ring->msg.if_id = 0;
	h2d_ring->msg.request_id = htol32(ring_to_create->create_req_id);
	h2d_ring->msg.flags = ctrl_ring->current_phase;
	h2d_ring->ring_id = htol16(DHD_H2D_RING_OFFSET(ring_to_create->idx));
	h2d_ring->ring_type = ring_type;
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
		ctrl_ring->seqnum % H2D_EPOCH_MODULO;
	ctrl_ring->seqnum++;

	/* Update the flow_ring's WRITE index */
	dhd_prot_ring_write_complete(dhd, ctrl_ring, h2d_ring,
		DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D);

	DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return ret;
err:
	DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

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

	/* XXX: Test casting ptr to uint16* for 32bit indices case on Big Endian */
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
#ifdef DHD_DMA_INDICES_SEQNUM
			if (prot->h2d_dma_indx_rd_copy_buf) {
				ptr = (uint8 *)(prot->h2d_dma_indx_rd_copy_buf);
			} else
#endif /* DHD_DMA_INDICES_SEQNUM */
			{
				ptr = (uint8 *)(prot->h2d_dma_indx_rd_buf.va);
			}
			offset = DHD_H2D_RING_OFFSET(ringid);
			break;

		case D2H_DMA_INDX_WR_UPD:
#ifdef DHD_DMA_INDICES_SEQNUM
			if (prot->d2h_dma_indx_wr_copy_buf) {
				ptr = (uint8 *)(prot->d2h_dma_indx_wr_copy_buf);
			} else
#endif /* DHD_DMA_INDICES_SEQNUM */
			{
				ptr = (uint8 *)(prot->d2h_dma_indx_wr_buf.va);
			}
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

	/* XXX: Test casting ptr to uint16* for 32bit indices case on Big Endian */
	data = LTOH16(*((uint16*)ptr));

	DHD_TRACE(("%s: data %d type %d ringid %d ptr 0x%p offset %d\n",
		__FUNCTION__, data, type, ringid, ptr, offset));

	return (data);

} /* dhd_prot_dma_indx_get */

#ifdef DHD_DMA_INDICES_SEQNUM
void
dhd_prot_write_host_seqnum(dhd_pub_t *dhd, uint32 seq_num)
{
	uint8 *ptr;
	dhd_prot_t *prot = dhd->prot;

	/* Update host sequence number in first four bytes of scratchbuf */
	ptr = (uint8 *)(prot->d2h_dma_scratch_buf.va);
	*(uint32*)ptr = htol32(seq_num);
	OSL_CACHE_FLUSH((void *)ptr, prot->d2h_dma_scratch_buf.len);

	DHD_TRACE(("%s: data %d ptr 0x%p\n", __FUNCTION__, seq_num, ptr));

} /* dhd_prot_dma_indx_set */

uint32
dhd_prot_read_seqnum(dhd_pub_t *dhd, bool host)
{
	uint8 *ptr;
	dhd_prot_t *prot = dhd->prot;
	uint32 data;

	OSL_CACHE_INV((void *)ptr, d2h_dma_scratch_buf.len);

	/* First four bytes of scratchbuf contains the host sequence number.
	 * Next four bytes of scratchbuf contains the Dongle sequence number.
	 */
	if (host) {
		ptr = (uint8 *)(prot->d2h_dma_scratch_buf.va);
		data = LTOH32(*((uint32*)ptr));
	} else {
		ptr = ((uint8 *)(prot->d2h_dma_scratch_buf.va) + sizeof(uint32));
		data = LTOH32(*((uint32*)ptr));
	}
	DHD_TRACE(("%s: data %d ptr 0x%p\n", __FUNCTION__, data, ptr));
	return data;
} /* dhd_prot_dma_indx_set */

void
dhd_prot_save_dmaidx(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	uint32 dngl_seqnum;

	dngl_seqnum = dhd_prot_read_seqnum(dhd, FALSE);

	DHD_TRACE(("%s: host_seqnum %u dngl_seqnum %u\n", __FUNCTION__,
			prot->host_seqnum, dngl_seqnum));
	if (prot->d2h_dma_indx_wr_copy_buf && prot->h2d_dma_indx_rd_copy_buf) {
		if (prot->host_seqnum == dngl_seqnum) {
			memcpy_s(prot->d2h_dma_indx_wr_copy_buf, prot->d2h_dma_indx_wr_copy_bufsz,
				prot->d2h_dma_indx_wr_buf.va, prot->d2h_dma_indx_wr_copy_bufsz);
			memcpy_s(prot->h2d_dma_indx_rd_copy_buf, prot->h2d_dma_indx_rd_copy_bufsz,
				prot->h2d_dma_indx_rd_buf.va, prot->h2d_dma_indx_rd_copy_bufsz);
			dhd_prot_write_host_seqnum(dhd, prot->host_seqnum);
			/* Ring DoorBell */
			dhd_prot_ring_doorbell(dhd, DHD_DMA_INDX_SEQ_H2D_DB_MAGIC);
			prot->host_seqnum++;
			prot->host_seqnum %= D2H_EPOCH_MODULO;
		}
	}
}

int
dhd_prot_dma_indx_copybuf_init(dhd_pub_t *dhd, uint32 buf_sz, uint8 type)
{
	dhd_prot_t *prot = dhd->prot;

	switch (type) {
		case D2H_DMA_INDX_WR_BUF:
			prot->d2h_dma_indx_wr_copy_buf = MALLOCZ(dhd->osh, buf_sz);
			if (prot->d2h_dma_indx_wr_copy_buf == NULL) {
				DHD_ERROR(("%s: MALLOC failed for size %d\n",
					__FUNCTION__, buf_sz));
				goto ret_no_mem;
			}
			prot->d2h_dma_indx_wr_copy_bufsz = buf_sz;
		break;

		case H2D_DMA_INDX_RD_BUF:
			prot->h2d_dma_indx_rd_copy_buf = MALLOCZ(dhd->osh, buf_sz);
			if (prot->h2d_dma_indx_rd_copy_buf == NULL) {
				DHD_ERROR(("%s: MALLOC failed for size %d\n",
					__FUNCTION__, buf_sz));
				goto ret_no_mem;
			}
			prot->h2d_dma_indx_rd_copy_bufsz = buf_sz;
			break;

		default:
			break;
	}
	return BCME_OK;
ret_no_mem:
	return BCME_NOMEM;

}
#endif /* DHD_DMA_INDICES_SEQNUM */

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

	/* XXX Double cache invalidate for ARM with L2 cache/prefetch */
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
	msgbuf_ring_t *ctrl_ring = &dhd->prot->h2dring_ctrl_subn;
	unsigned long flags;
	int num_post = 1;
	int i;

	DHD_MSGBUF_INFO(("%s Sending H2D MB data Req data 0x%04x\n",
		__FUNCTION__, mb_data));
	if (!ctrl_ring->inited) {
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
		DHD_RING_LOCK(ctrl_ring->ring_lock, flags);
		/* Request for ring buffer space */
		h2d_mb_data = (h2d_mailbox_data_t *)dhd_prot_alloc_ring_space(dhd,
			ctrl_ring, DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D,
			&alloced, FALSE);

		if (h2d_mb_data == NULL) {
			DHD_ERROR(("%s: FATAL: No space in control ring to send H2D Mb data\n",
				__FUNCTION__));
			DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);
			return BCME_NOMEM;
		}

		memset(h2d_mb_data, 0, sizeof(h2d_mailbox_data_t));
		/* Common msg buf hdr */
		h2d_mb_data->msg.msg_type = MSG_TYPE_H2D_MAILBOX_DATA;
		h2d_mb_data->msg.flags = ctrl_ring->current_phase;

		h2d_mb_data->msg.epoch =
			ctrl_ring->seqnum % H2D_EPOCH_MODULO;
		ctrl_ring->seqnum++;

		/* Update flow create message */
		h2d_mb_data->mail_box_data = htol32(mb_data);
#ifdef PCIE_INB_DW
		/* post device_wake first */
		if ((num_post == 2) && (i == 0)) {
			h2d_mb_data->mail_box_data = htol32(H2DMB_DS_DEVICE_WAKE);
		} else
#endif /* PCIE_INB_DW */
		{
			h2d_mb_data->mail_box_data = htol32(mb_data);
		}

		DHD_MSGBUF_INFO(("%s Send H2D MB data Req data 0x%04x\n", __FUNCTION__, mb_data));

		/* upd wrt ptr and raise interrupt */
		dhd_prot_ring_write_complete_mbdata(dhd, ctrl_ring, h2d_mb_data,
			DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D, mb_data);

		DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

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
	DHD_RING_LOCK(ctrl_ring->ring_lock, flags);

	/* Request for ctrl_ring buffer space */
	flow_create_rqst = (tx_flowring_create_request_t *)
		dhd_prot_alloc_ring_space(dhd, ctrl_ring, 1, &alloced, FALSE);

	if (flow_create_rqst == NULL) {
		dhd_prot_flowrings_pool_release(dhd, flow_ring_node->flowid, flow_ring);
		DHD_ERROR(("%s: Flow Create Req flowid %d - failure ring space\n",
			__FUNCTION__, flow_ring_node->flowid));
		DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);
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
	flow_create_rqst->max_items = htol16(flow_ring->max_items);
	flow_create_rqst->len_item = htol16(H2DRING_TXPOST_ITEMSIZE);
	flow_create_rqst->if_flags = 0;

#ifdef DHD_HP2P
	/* Create HPP flow ring if HP2P is enabled and TID=7  and AWDL interface */
	/* and traffic is not multicast */
	/* Allow infra interface only if user enabled hp2p_infra_enable thru iovar */
	if (dhd->hp2p_capable && dhd->hp2p_ring_more &&
		flow_ring_node->flow_info.tid == HP2P_PRIO &&
		(dhd->hp2p_infra_enable || flow_create_rqst->msg.if_id) &&
		!ETHER_ISMULTI(flow_create_rqst->da)) {
		flow_create_rqst->if_flags |= BCMPCIE_FLOW_RING_INTF_HP2P;
		flow_ring_node->hp2p_ring = TRUE;
		/* Allow multiple HP2P Flow if mf override is enabled */
		if (!dhd->hp2p_mf_enable) {
			dhd->hp2p_ring_more = FALSE;
		}

		DHD_ERROR(("%s: flow ring for HP2P tid = %d flowid = %d\n",
				__FUNCTION__, flow_ring_node->flow_info.tid,
				flow_ring_node->flowid));
	}
#endif /* DHD_HP2P */

	/* definition for ifrm mask : bit0:d11ac core, bit1:d11ad core
	 * currently it is not used for priority. so uses solely for ifrm mask
	 */
	if (IFRM_ACTIVE(dhd))
		flow_create_rqst->priority_ifrmmask = (1 << IFRM_DEV_0);

	DHD_ERROR(("%s: Send Flow Create Req flow ID %d for peer " MACDBG
		" prio %d ifindex %d items %d\n", __FUNCTION__, flow_ring_node->flowid,
		MAC2STRDBG(flow_ring_node->flow_info.da), flow_ring_node->flow_info.tid,
		flow_ring_node->flow_info.ifindex, flow_ring->max_items));

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

	DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

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

#if !defined(BCM_ROUTER_DHD)
static void
dhd_prot_process_h2d_ring_create_complete(dhd_pub_t *dhd, void *buf)
{
	h2d_ring_create_response_t *resp = (h2d_ring_create_response_t *)buf;
	DHD_INFO(("%s ring create Response status = %d ring %d, id 0x%04x\n", __FUNCTION__,
		ltoh16(resp->cmplt.status),
		ltoh16(resp->cmplt.ring_id),
		ltoh32(resp->cmn_hdr.request_id)));
	if ((ltoh32(resp->cmn_hdr.request_id) != DHD_H2D_DBGRING_REQ_PKTID) &&
		(ltoh32(resp->cmn_hdr.request_id) != DHD_H2D_BTLOGRING_REQ_PKTID)) {
		DHD_ERROR(("invalid request ID with h2d ring create complete\n"));
		return;
	}
	if (dhd->prot->h2dring_info_subn->create_req_id == ltoh32(resp->cmn_hdr.request_id) &&
		!dhd->prot->h2dring_info_subn->create_pending) {
		DHD_ERROR(("info ring create status for not pending submit ring\n"));
	}
#ifdef BTLOG
	if (dhd->prot->h2dring_btlog_subn &&
		dhd->prot->h2dring_btlog_subn->create_req_id == ltoh32(resp->cmn_hdr.request_id) &&
		!dhd->prot->h2dring_btlog_subn->create_pending) {
		DHD_ERROR(("btlog ring create status for not pending submit ring\n"));
	}
#endif	/* BTLOG */

	if (ltoh16(resp->cmplt.status) != BCMPCIE_SUCCESS) {
		DHD_ERROR(("info/btlog ring create failed with status %d\n",
			ltoh16(resp->cmplt.status)));
		return;
	}
	if (dhd->prot->h2dring_info_subn->create_req_id == ltoh32(resp->cmn_hdr.request_id)) {
		dhd->prot->h2dring_info_subn->create_pending = FALSE;
		dhd->prot->h2dring_info_subn->inited = TRUE;
		DHD_ERROR(("info buffer post after ring create\n"));
		dhd_prot_infobufpost(dhd, dhd->prot->h2dring_info_subn);
	}
#ifdef BTLOG
	if (dhd->prot->h2dring_btlog_subn &&
		dhd->prot->h2dring_btlog_subn->create_req_id == ltoh32(resp->cmn_hdr.request_id)) {
		dhd->prot->h2dring_btlog_subn->create_pending = FALSE;
		dhd->prot->h2dring_btlog_subn->inited = TRUE;
		DHD_ERROR(("btlog buffer post after ring create\n"));
		dhd_prot_infobufpost(dhd, dhd->prot->h2dring_btlog_subn);
	}
#endif	/* BTLOG */
}
#endif /* !BCM_ROUTER_DHD */

static void
dhd_prot_process_d2h_ring_create_complete(dhd_pub_t *dhd, void *buf)
{
	d2h_ring_create_response_t *resp = (d2h_ring_create_response_t *)buf;
	DHD_ERROR(("%s ring create Response status = %d ring %d, id 0x%04x\n", __FUNCTION__,
		ltoh16(resp->cmplt.status),
		ltoh16(resp->cmplt.ring_id),
		ltoh32(resp->cmn_hdr.request_id)));
	if ((ltoh32(resp->cmn_hdr.request_id) != DHD_D2H_DBGRING_REQ_PKTID) &&
		(ltoh32(resp->cmn_hdr.request_id) != DHD_D2H_BTLOGRING_REQ_PKTID) &&
#ifdef DHD_HP2P
		(ltoh32(resp->cmn_hdr.request_id) != DHD_D2H_HPPRING_TXREQ_PKTID) &&
		(ltoh32(resp->cmn_hdr.request_id) != DHD_D2H_HPPRING_RXREQ_PKTID) &&
#endif /* DHD_HP2P */
		TRUE) {
		DHD_ERROR(("invalid request ID with d2h ring create complete\n"));
		return;
	}
	if (ltoh32(resp->cmn_hdr.request_id) == DHD_D2H_DBGRING_REQ_PKTID) {
#ifdef EWP_EDL
		if (!dhd->dongle_edl_support)
#endif
		{

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
#ifdef EWP_EDL
		else {
			if (!dhd->prot->d2hring_edl->create_pending) {
				DHD_ERROR(("edl ring create status for not pending cpl ring\n"));
				return;
			}

			if (ltoh16(resp->cmplt.status) != BCMPCIE_SUCCESS) {
				DHD_ERROR(("edl cpl ring create failed with status %d\n",
					ltoh16(resp->cmplt.status)));
				return;
			}
			dhd->prot->d2hring_edl->create_pending = FALSE;
			dhd->prot->d2hring_edl->inited = TRUE;
		}
#endif /* EWP_EDL */
	}

#ifdef BTLOG
	if (ltoh32(resp->cmn_hdr.request_id) == DHD_D2H_BTLOGRING_REQ_PKTID) {
		if (!dhd->prot->d2hring_btlog_cpln->create_pending) {
			DHD_ERROR(("btlog ring create status for not pending cpl ring\n"));
			return;
		}

		if (ltoh16(resp->cmplt.status) != BCMPCIE_SUCCESS) {
			DHD_ERROR(("btlog cpl ring create failed with status %d\n",
				ltoh16(resp->cmplt.status)));
			return;
		}
		dhd->prot->d2hring_btlog_cpln->create_pending = FALSE;
		dhd->prot->d2hring_btlog_cpln->inited = TRUE;
	}
#endif	/* BTLOG */
#ifdef DHD_HP2P
	if (dhd->prot->d2hring_hp2p_txcpl &&
		ltoh32(resp->cmn_hdr.request_id) == DHD_D2H_HPPRING_TXREQ_PKTID) {
		if (!dhd->prot->d2hring_hp2p_txcpl->create_pending) {
			DHD_ERROR(("HPP tx ring create status for not pending cpl ring\n"));
			return;
		}

		if (ltoh16(resp->cmplt.status) != BCMPCIE_SUCCESS) {
			DHD_ERROR(("HPP tx cpl ring create failed with status %d\n",
				ltoh16(resp->cmplt.status)));
			return;
		}
		dhd->prot->d2hring_hp2p_txcpl->create_pending = FALSE;
		dhd->prot->d2hring_hp2p_txcpl->inited = TRUE;
	}
	if (dhd->prot->d2hring_hp2p_rxcpl &&
		ltoh32(resp->cmn_hdr.request_id) == DHD_D2H_HPPRING_RXREQ_PKTID) {
		if (!dhd->prot->d2hring_hp2p_rxcpl->create_pending) {
			DHD_ERROR(("HPP rx ring create status for not pending cpl ring\n"));
			return;
		}

		if (ltoh16(resp->cmplt.status) != BCMPCIE_SUCCESS) {
			DHD_ERROR(("HPP rx cpl ring create failed with status %d\n",
				ltoh16(resp->cmplt.status)));
			return;
		}
		dhd->prot->d2hring_hp2p_rxcpl->create_pending = FALSE;
		dhd->prot->d2hring_hp2p_rxcpl->inited = TRUE;
	}
#endif /* DHD_HP2P */
}

static void
dhd_prot_process_d2h_mb_data(dhd_pub_t *dhd, void* buf)
{
	d2h_mailbox_data_t *d2h_data;

	d2h_data = (d2h_mailbox_data_t *)buf;
	DHD_MSGBUF_INFO(("%s dhd_prot_process_d2h_mb_data, 0x%04x\n", __FUNCTION__,
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

void dhd_prot_print_flow_ring(dhd_pub_t *dhd, void *msgbuf_flow_info, bool h2d,
	struct bcmstrbuf *strbuf, const char * fmt)
{
	const char *default_fmt =
		"TRD:%d HLRD:%d HDRD:%d TWR:%d HLWR:%d HDWR:%d  BASE(VA) %p BASE(PA) %x:%x SIZE %d "
		"WORK_ITEM_SIZE %d MAX_WORK_ITEMS %d TOTAL_SIZE %d\n";
	msgbuf_ring_t *flow_ring = (msgbuf_ring_t *)msgbuf_flow_info;
	uint16 rd, wr, drd = 0, dwr = 0;
	uint32 dma_buf_len = flow_ring->max_items * flow_ring->item_len;

	if (fmt == NULL) {
		fmt = default_fmt;
	}

	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("%s: Skip dumping flowring due to Link down\n", __FUNCTION__));
		return;
	}

	dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, flow_ring->idx);
	dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, flow_ring->idx);
	if (dhd->dma_d2h_ring_upd_support) {
		if (h2d) {
			drd = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, flow_ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_WR_UPD, flow_ring->idx);
		} else {
			drd = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, flow_ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, flow_ring->idx);
		}
	}
	bcm_bprintf(strbuf, fmt, rd, flow_ring->rd, drd, wr, flow_ring->wr, dwr,
		flow_ring->dma_buf.va,
		ltoh32(flow_ring->base_addr.high_addr),
		ltoh32(flow_ring->base_addr.low_addr),
		flow_ring->item_len, flow_ring->max_items,
		dma_buf_len);
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
#ifdef BTLOG
	bcm_bprintf(strbuf, "max BTLOG bufs to post: %d, \t posted %d \n",
		dhd->prot->max_btlogbufpost, dhd->prot->btlogbufpost);
#endif	/* BTLOG */
	bcm_bprintf(strbuf, "max event bufs to post: %d, \t posted %d \n",
		dhd->prot->max_eventbufpost, dhd->prot->cur_event_bufs_posted);
	bcm_bprintf(strbuf, "max ioctlresp bufs to post: %d, \t posted %d \n",
		dhd->prot->max_ioctlrespbufpost, dhd->prot->cur_ioctlresp_bufs_posted);
	bcm_bprintf(strbuf, "max RX bufs to post: %d, \t posted %d \n",
		dhd->prot->max_rxbufpost, dhd->prot->rxbufpost);

	bcm_bprintf(strbuf, "Total RX bufs posted: %d, \t RX cpl got %d \n",
		dhd->prot->tot_rxbufpost, dhd->prot->tot_rxcpl);

	bcm_bprintf(strbuf, "Total TX packets: %lu, \t TX cpl got %lu \n",
		dhd->actual_tx_pkts, dhd->tot_txcpl);

	bcm_bprintf(strbuf,
		"%14s %18s %18s %17s %17s %14s %14s %10s\n",
		"Type", "TRD: HLRD: HDRD", "TWR: HLWR: HDWR", "BASE(VA)", "BASE(PA)",
		"WORK_ITEM_SIZE", "MAX_WORK_ITEMS", "TOTAL_SIZE");
	bcm_bprintf(strbuf, "%14s", "H2DCtrlPost");
	dhd_prot_print_flow_ring(dhd, &prot->h2dring_ctrl_subn, TRUE, strbuf,
		" %5d:%5d:%5d %5d:%5d:%5d %17p %8x:%8x %14d %14d %10d\n");
	bcm_bprintf(strbuf, "%14s", "D2HCtrlCpl");
	dhd_prot_print_flow_ring(dhd, &prot->d2hring_ctrl_cpln, FALSE, strbuf,
		" %5d:%5d:%5d %5d:%5d:%5d %17p %8x:%8x %14d %14d %10d\n");
	bcm_bprintf(strbuf, "%14s", "H2DRxPost");
	dhd_prot_print_flow_ring(dhd, &prot->h2dring_rxp_subn, TRUE, strbuf,
		" %5d:%5d:%5d %5d:%5d:%5d %17p %8x:%8x %14d %14d %10d\n");
	bcm_bprintf(strbuf, "%14s", "D2HRxCpl");
	dhd_prot_print_flow_ring(dhd, &prot->d2hring_rx_cpln, FALSE, strbuf,
		" %5d:%5d:%5d %5d:%5d:%5d %17p %8x:%8x %14d %14d %10d\n");
	bcm_bprintf(strbuf, "%14s", "D2HTxCpl");
	dhd_prot_print_flow_ring(dhd, &prot->d2hring_tx_cpln, FALSE, strbuf,
		" %5d:%5d:%5d %5d:%5d:%5d %17p %8x:%8x %14d %14d %10d\n");
	if (dhd->prot->h2dring_info_subn != NULL && dhd->prot->d2hring_info_cpln != NULL) {
		bcm_bprintf(strbuf, "%14s", "H2DRingInfoSub");
		dhd_prot_print_flow_ring(dhd, prot->h2dring_info_subn, TRUE, strbuf,
			" %5d:%5d:%5d %5d:%5d:%5d %17p %8x:%8x %14d %14d %10d\n");
		bcm_bprintf(strbuf, "%14s", "D2HRingInfoCpl");
		dhd_prot_print_flow_ring(dhd, prot->d2hring_info_cpln, FALSE, strbuf,
			" %5d:%5d:%5d %5d:%5d:%5d %17p %8x:%8x %14d %14d %10d\n");
	}
	if (dhd->prot->d2hring_edl != NULL) {
		bcm_bprintf(strbuf, "%14s", "D2HRingEDL");
		dhd_prot_print_flow_ring(dhd, prot->d2hring_edl, FALSE, strbuf,
			" %5d:%5d:%5d %5d:%5d:%5d %17p %8x:%8x %14d %14d %10d\n");
	}

	bcm_bprintf(strbuf, "active_tx_count %d	 pktidmap_avail(ctrl/rx/tx) %d %d %d\n",
		OSL_ATOMIC_READ(dhd->osh, &dhd->prot->active_tx_count),
		DHD_PKTID_AVAIL(dhd->prot->pktid_ctrl_map),
		DHD_PKTID_AVAIL(dhd->prot->pktid_rx_map),
		DHD_PKTID_AVAIL(dhd->prot->pktid_tx_map));

#if defined(BCMINTERNAL) && defined(DHD_DBG_DUMP)
	dhd_prot_ioctl_dump(dhd->prot, strbuf);
#endif /* defined(BCMINTERNAL) && defined(DHD_DBG_DUMP) */
#ifdef DHD_MMIO_TRACE
	dhd_dump_bus_mmio_trace(dhd->bus, strbuf);
#endif /* DHD_MMIO_TRACE */
	dhd_dump_bus_ds_trace(dhd->bus, strbuf);
#ifdef DHD_FLOW_RING_STATUS_TRACE
	dhd_dump_bus_flow_ring_status_isr_trace(dhd->bus, strbuf);
	dhd_dump_bus_flow_ring_status_dpc_trace(dhd->bus, strbuf);
#endif /* DHD_FLOW_RING_STATUS_TRACE */
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

	DHD_RING_LOCK(ring->ring_lock, flags);

	/* Request for ring buffer space */
	flow_delete_rqst = (tx_flowring_delete_request_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (flow_delete_rqst == NULL) {
		DHD_RING_UNLOCK(ring->ring_lock, flags);
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

	DHD_ERROR(("%s: Send Flow Delete Req RING ID %d for peer %pM"
		" prio %d ifindex %d\n", __FUNCTION__, flow_ring_node->flowid,
		flow_ring_node->flow_info.da, flow_ring_node->flow_info.tid,
		flow_ring_node->flow_info.ifindex));

	/* update ring's WR index and ring doorbell to dongle */
	dhd_prot_ring_write_complete(dhd, ring, flow_delete_rqst, 1);

	DHD_RING_UNLOCK(ring->ring_lock, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif
	return BCME_OK;
}

static void
BCMFASTPATH(dhd_prot_flow_ring_fastdelete)(dhd_pub_t *dhd, uint16 flowid, uint16 rd_idx)
{
	flow_ring_node_t *flow_ring_node = DHD_FLOW_RING(dhd, flowid);
	msgbuf_ring_t *ring = (msgbuf_ring_t *)flow_ring_node->prot_info;
	host_txbuf_cmpl_t txstatus;
	host_txbuf_post_t *txdesc;
	uint16 wr_idx;

	DHD_INFO(("%s: FAST delete ring, flowid=%d, rd_idx=%d, wr_idx=%d\n",
		__FUNCTION__, flowid, rd_idx, ring->wr));

	memset(&txstatus, 0, sizeof(txstatus));
	txstatus.compl_hdr.flow_ring_id = flowid;
	txstatus.cmn_hdr.if_id = flow_ring_node->flow_info.ifindex;
	wr_idx = ring->wr;

	while (wr_idx != rd_idx) {
		if (wr_idx)
			wr_idx--;
		else
			wr_idx = ring->max_items - 1;
		txdesc = (host_txbuf_post_t *)((char *)DHD_RING_BGN_VA(ring) +
			(wr_idx * ring->item_len));
		txstatus.cmn_hdr.request_id = txdesc->cmn_hdr.request_id;
		dhd_prot_txstatus_process(dhd, &txstatus);
	}
}

static void
dhd_prot_flow_ring_delete_response_process(dhd_pub_t *dhd, void *msg)
{
	tx_flowring_delete_response_t *flow_delete_resp = (tx_flowring_delete_response_t *)msg;

	DHD_ERROR(("%s: Flow Delete Response status = %d Flow %d\n", __FUNCTION__,
		flow_delete_resp->cmplt.status, flow_delete_resp->cmplt.flow_ring_id));

	if (dhd->fast_delete_ring_support) {
		dhd_prot_flow_ring_fastdelete(dhd, flow_delete_resp->cmplt.flow_ring_id,
			flow_delete_resp->read_idx);
	}
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

	DHD_RING_LOCK(ring->ring_lock, flags);

	/* Request for ring buffer space */
	flow_flush_rqst = (tx_flowring_flush_request_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);
	if (flow_flush_rqst == NULL) {
		DHD_RING_UNLOCK(ring->ring_lock, flags);
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

	DHD_RING_UNLOCK(ring->ring_lock, flags);

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
	DHD_RING_LOCK(ctrl_ring->ring_lock, flags);
	msg_start = dhd_prot_alloc_ring_space(dhd, ctrl_ring, d2h_rings, &alloced, TRUE);

	if (msg_start == NULL) {
		DHD_ERROR(("%s Msgbuf no space for %d D2H ring config soft doorbells\n",
			__FUNCTION__, d2h_rings));
		DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);
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

	DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

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

#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
void
copy_ext_trap_sig(dhd_pub_t *dhd, trap_t *tr)
{
	uint32 *ext_data = dhd->extended_trap_data;
	hnd_ext_trap_hdr_t *hdr;
	const bcm_tlv_t *tlv;

	if (ext_data == NULL) {
		return;
	}
	/* First word is original trap_data */
	ext_data++;

	/* Followed by the extended trap data header */
	hdr = (hnd_ext_trap_hdr_t *)ext_data;

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_SIGNATURE);
	if (tlv) {
		memcpy(tr, &tlv->data, sizeof(struct _trap_struct));
	}
}
#define TRAP_T_NAME_OFFSET(var) {#var, OFFSETOF(trap_t, var)}

typedef struct {
	char name[HANG_INFO_TRAP_T_NAME_MAX];
	uint32 offset;
} hang_info_trap_t;

#ifdef DHD_EWPR_VER2
static hang_info_trap_t hang_info_trap_tbl[] = {
	{"reason", 0},
	{"ver", VENDOR_SEND_HANG_EXT_INFO_VER},
	{"stype", 0},
	TRAP_T_NAME_OFFSET(type),
	TRAP_T_NAME_OFFSET(epc),
	{"resrvd", 0},
	{"resrvd", 0},
	{"resrvd", 0},
	{"resrvd", 0},
	{"", 0}
};
#else
static hang_info_trap_t hang_info_trap_tbl[] = {
	{"reason", 0},
	{"ver", VENDOR_SEND_HANG_EXT_INFO_VER},
	{"stype", 0},
	TRAP_T_NAME_OFFSET(type),
	TRAP_T_NAME_OFFSET(epc),
	TRAP_T_NAME_OFFSET(cpsr),
	TRAP_T_NAME_OFFSET(spsr),
	TRAP_T_NAME_OFFSET(r0),
	TRAP_T_NAME_OFFSET(r1),
	TRAP_T_NAME_OFFSET(r2),
	TRAP_T_NAME_OFFSET(r3),
	TRAP_T_NAME_OFFSET(r4),
	TRAP_T_NAME_OFFSET(r5),
	TRAP_T_NAME_OFFSET(r6),
	TRAP_T_NAME_OFFSET(r7),
	TRAP_T_NAME_OFFSET(r8),
	TRAP_T_NAME_OFFSET(r9),
	TRAP_T_NAME_OFFSET(r10),
	TRAP_T_NAME_OFFSET(r11),
	TRAP_T_NAME_OFFSET(r12),
	TRAP_T_NAME_OFFSET(r13),
	TRAP_T_NAME_OFFSET(r14),
	TRAP_T_NAME_OFFSET(pc),
	{"", 0}
};
#endif /* DHD_EWPR_VER2 */

#define TAG_TRAP_IS_STATE(tag) \
	((tag == TAG_TRAP_MEMORY) || (tag == TAG_TRAP_PCIE_Q) || \
	(tag == TAG_TRAP_WLC_STATE) || (tag == TAG_TRAP_LOG_DATA) || \
	(tag == TAG_TRAP_CODE))

static void
copy_hang_info_head(char *dest, trap_t *src, int len, int field_name,
		int *bytes_written, int *cnt, char *cookie)
{
	uint8 *ptr;
	int remain_len;
	int i;

	ptr = (uint8 *)src;

	memset(dest, 0, len);
	remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;

	/* hang reason, hang info ver */
	for (i = 0; (i < HANG_INFO_TRAP_T_SUBTYPE_IDX) && (*cnt < HANG_FIELD_CNT_MAX);
			i++, (*cnt)++) {
		if (field_name) {
			remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
			*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%s:%c",
					hang_info_trap_tbl[i].name, HANG_KEY_DEL);
		}
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%d%c",
				hang_info_trap_tbl[i].offset, HANG_KEY_DEL);

	}

	if (*cnt < HANG_FIELD_CNT_MAX) {
		if (field_name) {
			remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
			*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%s:%c",
					"cookie", HANG_KEY_DEL);
		}
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%s%c",
				cookie, HANG_KEY_DEL);
		(*cnt)++;
	}

	if (*cnt < HANG_FIELD_CNT_MAX) {
		if (field_name) {
			remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
			*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%s:%c",
					hang_info_trap_tbl[HANG_INFO_TRAP_T_SUBTYPE_IDX].name,
					HANG_KEY_DEL);
		}
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%08x%c",
				hang_info_trap_tbl[HANG_INFO_TRAP_T_SUBTYPE_IDX].offset,
				HANG_KEY_DEL);
		(*cnt)++;
	}

	if (*cnt < HANG_FIELD_CNT_MAX) {
		if (field_name) {
			remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
			*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%s:%c",
					hang_info_trap_tbl[HANG_INFO_TRAP_T_EPC_IDX].name,
					HANG_KEY_DEL);
		}
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%08x%c",
				*(uint32 *)
				(ptr + hang_info_trap_tbl[HANG_INFO_TRAP_T_EPC_IDX].offset),
				HANG_KEY_DEL);
		(*cnt)++;
	}
#ifdef DHD_EWPR_VER2
	/* put 0 for HG03 ~ HG06 (reserved for future use) */
	for (i = 0; (i < HANG_INFO_BIGDATA_EXTRA_KEY) && (*cnt < HANG_FIELD_CNT_MAX);
			i++, (*cnt)++) {
		if (field_name) {
			remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
			*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%s:%c",
				hang_info_trap_tbl[HANG_INFO_TRAP_T_EXTRA_KEY_IDX+i].name,
				HANG_KEY_DEL);
		}
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%d%c",
			hang_info_trap_tbl[HANG_INFO_TRAP_T_EXTRA_KEY_IDX+i].offset,
			HANG_KEY_DEL);
	}
#endif /* DHD_EWPR_VER2 */
}
#ifndef DHD_EWPR_VER2
static void
copy_hang_info_trap_t(char *dest, trap_t *src, int len, int field_name,
		int *bytes_written, int *cnt, char *cookie)
{
	uint8 *ptr;
	int remain_len;
	int i;

	ptr = (uint8 *)src;

	remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;

	for (i = HANG_INFO_TRAP_T_OFFSET_IDX;
			(hang_info_trap_tbl[i].name[0] != 0) && (*cnt < HANG_FIELD_CNT_MAX);
			i++, (*cnt)++) {
		if (field_name) {
			remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
			*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%c%s:",
					HANG_RAW_DEL, hang_info_trap_tbl[i].name);
		}
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%c%08x",
				HANG_RAW_DEL, *(uint32 *)(ptr + hang_info_trap_tbl[i].offset));
	}
}

static void
copy_hang_info_stack(dhd_pub_t *dhd, char *dest, int *bytes_written, int *cnt)
{
	int remain_len;
	int i = 0;
	const uint32 *stack;
	uint32 *ext_data = dhd->extended_trap_data;
	hnd_ext_trap_hdr_t *hdr;
	const bcm_tlv_t *tlv;
	int remain_stack_cnt = 0;
	uint32 dummy_data = 0;
	int bigdata_key_stack_cnt = 0;

	if (ext_data == NULL) {
		return;
	}
	/* First word is original trap_data */
	ext_data++;

	/* Followed by the extended trap data header */
	hdr = (hnd_ext_trap_hdr_t *)ext_data;

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_STACK);

	remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	if (tlv) {
		stack = (const uint32 *)tlv->data;
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len,
				"%08x", *(uint32 *)(stack++));
		(*cnt)++;
		if (*cnt >= HANG_FIELD_CNT_MAX) {
			return;
		}
		for (i = 1; i < (uint32)(tlv->len / sizeof(uint32)); i++, bigdata_key_stack_cnt++) {
			remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
			/* Raw data for bigdata use '_' and Key data for bigdata use space */
			*bytes_written += scnprintf(&dest[*bytes_written], remain_len,
				"%c%08x",
				i <= HANG_INFO_BIGDATA_KEY_STACK_CNT ? HANG_KEY_DEL : HANG_RAW_DEL,
				*(uint32 *)(stack++));

			(*cnt)++;
			if ((*cnt >= HANG_FIELD_CNT_MAX) ||
					(i >= HANG_FIELD_TRAP_T_STACK_CNT_MAX)) {
				return;
			}
		}
	}

	remain_stack_cnt = HANG_FIELD_TRAP_T_STACK_CNT_MAX - i;

	for (i = 0; i < remain_stack_cnt; i++) {
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%c%08x",
				HANG_RAW_DEL, dummy_data);
		(*cnt)++;
		if (*cnt >= HANG_FIELD_CNT_MAX) {
			return;
		}
	}
	GCC_DIAGNOSTIC_POP();

}

static void
copy_hang_info_specific(dhd_pub_t *dhd, char *dest, int *bytes_written, int *cnt)
{
	int remain_len;
	int i;
	const uint32 *data;
	uint32 *ext_data = dhd->extended_trap_data;
	hnd_ext_trap_hdr_t *hdr;
	const bcm_tlv_t *tlv;
	int remain_trap_data = 0;
	uint8 buf_u8[sizeof(uint32)] = { 0, };
	const uint8 *p_u8;

	if (ext_data == NULL) {
		return;
	}
	/* First word is original trap_data */
	ext_data++;

	/* Followed by the extended trap data header */
	hdr = (hnd_ext_trap_hdr_t *)ext_data;

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_SIGNATURE);
	if (tlv) {
		/* header include tlv hader */
		remain_trap_data = (hdr->len - tlv->len - sizeof(uint16));
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_STACK);
	if (tlv) {
		/* header include tlv hader */
		remain_trap_data -= (tlv->len + sizeof(uint16));
	}

	data = (const uint32 *)(hdr->data + (hdr->len  - remain_trap_data));

	remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;

	for (i = 0; i < (uint32)(remain_trap_data / sizeof(uint32)) && *cnt < HANG_FIELD_CNT_MAX;
			i++, (*cnt)++) {
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%c%08x",
				HANG_RAW_DEL, *(uint32 *)(data++));
		GCC_DIAGNOSTIC_POP();
	}

	if (*cnt >= HANG_FIELD_CNT_MAX) {
		return;
	}

	remain_trap_data -= (sizeof(uint32) * i);

	if (remain_trap_data > sizeof(buf_u8)) {
		DHD_ERROR(("%s: resize remain_trap_data\n", __FUNCTION__));
		remain_trap_data =  sizeof(buf_u8);
	}

	if (remain_trap_data) {
		p_u8 = (const uint8 *)data;
		for (i = 0; i < remain_trap_data; i++) {
			buf_u8[i] = *(const uint8 *)(p_u8++);
		}

		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;
		*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%c%08x",
				HANG_RAW_DEL, ltoh32_ua(buf_u8));
		(*cnt)++;
	}
}
#endif /* DHD_EWPR_VER2 */

static void
get_hang_info_trap_subtype(dhd_pub_t *dhd, uint32 *subtype)
{
	uint32 i;
	uint32 *ext_data = dhd->extended_trap_data;
	hnd_ext_trap_hdr_t *hdr;
	const bcm_tlv_t *tlv;

	/* First word is original trap_data */
	ext_data++;

	/* Followed by the extended trap data header */
	hdr = (hnd_ext_trap_hdr_t *)ext_data;

	/* Dump a list of all tags found  before parsing data */
	for (i = TAG_TRAP_DEEPSLEEP; i < TAG_TRAP_LAST; i++) {
		tlv = bcm_parse_tlvs(hdr->data, hdr->len, i);
		if (tlv) {
			if (!TAG_TRAP_IS_STATE(i)) {
				*subtype = i;
				return;
			}
		}
	}
}
#ifdef DHD_EWPR_VER2
static void
copy_hang_info_etd_base64(dhd_pub_t *dhd, char *dest, int *bytes_written, int *cnt)
{
	int remain_len;
	uint32 *ext_data = dhd->extended_trap_data;
	hnd_ext_trap_hdr_t *hdr;
	char *base64_out = NULL;
	int base64_cnt;
	int max_base64_len = HANG_INFO_BASE64_BUFFER_SIZE;

	if (ext_data == NULL) {
		return;
	}
	/* First word is original trap_data */
	ext_data++;

	/* Followed by the extended trap data header */
	hdr = (hnd_ext_trap_hdr_t *)ext_data;

	remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - *bytes_written;

	if (remain_len <= 0) {
		DHD_ERROR(("%s: no space to put etd\n", __FUNCTION__));
		return;
	}

	if (remain_len < max_base64_len) {
		DHD_ERROR(("%s: change max base64 length to remain length %d\n", __FUNCTION__,
			remain_len));
		max_base64_len = remain_len;
	}

	base64_out = MALLOCZ(dhd->osh, HANG_INFO_BASE64_BUFFER_SIZE);
	if (base64_out == NULL) {
		DHD_ERROR(("%s: MALLOC failed for size %d\n",
			__FUNCTION__, HANG_INFO_BASE64_BUFFER_SIZE));
		return;
	}

	if (hdr->len > 0) {
		base64_cnt = dhd_base64_encode(hdr->data, hdr->len, base64_out, max_base64_len);
		if (base64_cnt == 0) {
			DHD_ERROR(("%s: base64 encoding error\n", __FUNCTION__));
		}
	}

	*bytes_written += scnprintf(&dest[*bytes_written], remain_len, "%s",
			base64_out);
	(*cnt)++;
	MFREE(dhd->osh, base64_out, HANG_INFO_BASE64_BUFFER_SIZE);
}
#endif /* DHD_EWPR_VER2 */

void
copy_hang_info_trap(dhd_pub_t *dhd)
{
	trap_t tr;
	int bytes_written;
	int trap_subtype = 0;

	if (!dhd || !dhd->hang_info) {
		DHD_ERROR(("%s dhd=%p hang_info=%p\n", __FUNCTION__,
			dhd, (dhd ? dhd->hang_info : NULL)));
		return;
	}

	if (!dhd->dongle_trap_occured) {
		DHD_ERROR(("%s: dongle_trap_occured is FALSE\n", __FUNCTION__));
		return;
	}

	memset(&tr, 0x00, sizeof(struct _trap_struct));

	copy_ext_trap_sig(dhd, &tr);
	get_hang_info_trap_subtype(dhd, &trap_subtype);

	hang_info_trap_tbl[HANG_INFO_TRAP_T_REASON_IDX].offset = HANG_REASON_DONGLE_TRAP;
	hang_info_trap_tbl[HANG_INFO_TRAP_T_SUBTYPE_IDX].offset = trap_subtype;

	bytes_written = 0;
	dhd->hang_info_cnt = 0;
	get_debug_dump_time(dhd->debug_dump_time_hang_str);
	copy_debug_dump_time(dhd->debug_dump_time_str, dhd->debug_dump_time_hang_str);

	copy_hang_info_head(dhd->hang_info, &tr, VENDOR_SEND_HANG_EXT_INFO_LEN, FALSE,
			&bytes_written, &dhd->hang_info_cnt, dhd->debug_dump_time_hang_str);

	DHD_INFO(("hang info head cnt: %d len: %d data: %s\n",
		dhd->hang_info_cnt, (int)strlen(dhd->hang_info), dhd->hang_info));

	clear_debug_dump_time(dhd->debug_dump_time_hang_str);

#ifdef DHD_EWPR_VER2
	/* stack info & trap info are included in etd data */

	/* extended trap data dump */
	if (dhd->hang_info_cnt < HANG_FIELD_CNT_MAX) {
		copy_hang_info_etd_base64(dhd, dhd->hang_info, &bytes_written, &dhd->hang_info_cnt);
		DHD_INFO(("hang info specific cnt: %d len: %d data: %s\n",
			dhd->hang_info_cnt, (int)strlen(dhd->hang_info), dhd->hang_info));
	}
#else
	if (dhd->hang_info_cnt < HANG_FIELD_CNT_MAX) {
		copy_hang_info_stack(dhd, dhd->hang_info, &bytes_written, &dhd->hang_info_cnt);
		DHD_INFO(("hang info stack cnt: %d len: %d data: %s\n",
			dhd->hang_info_cnt, (int)strlen(dhd->hang_info), dhd->hang_info));
	}

	if (dhd->hang_info_cnt < HANG_FIELD_CNT_MAX) {
		copy_hang_info_trap_t(dhd->hang_info, &tr, VENDOR_SEND_HANG_EXT_INFO_LEN, FALSE,
				&bytes_written, &dhd->hang_info_cnt, dhd->debug_dump_time_hang_str);
		DHD_INFO(("hang info trap_t cnt: %d len: %d data: %s\n",
			dhd->hang_info_cnt, (int)strlen(dhd->hang_info), dhd->hang_info));
	}

	if (dhd->hang_info_cnt < HANG_FIELD_CNT_MAX) {
		copy_hang_info_specific(dhd, dhd->hang_info, &bytes_written, &dhd->hang_info_cnt);
		DHD_INFO(("hang info specific cnt: %d len: %d data: %s\n",
			dhd->hang_info_cnt, (int)strlen(dhd->hang_info), dhd->hang_info));
	}
#endif /* DHD_EWPR_VER2 */
}

void
copy_hang_info_linkdown(dhd_pub_t *dhd)
{
	int bytes_written = 0;
	int remain_len;

	if (!dhd || !dhd->hang_info) {
		DHD_ERROR(("%s dhd=%p hang_info=%p\n", __FUNCTION__,
			dhd, (dhd ? dhd->hang_info : NULL)));
		return;
	}

	if (!dhd->bus->is_linkdown) {
		DHD_ERROR(("%s: link down is not happened\n", __FUNCTION__));
		return;
	}

	dhd->hang_info_cnt = 0;

	get_debug_dump_time(dhd->debug_dump_time_hang_str);
	copy_debug_dump_time(dhd->debug_dump_time_str, dhd->debug_dump_time_hang_str);

	/* hang reason code (0x8808) */
	if (dhd->hang_info_cnt < HANG_FIELD_CNT_MAX) {
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - bytes_written;
		bytes_written += scnprintf(&dhd->hang_info[bytes_written], remain_len, "%d%c",
				HANG_REASON_PCIE_LINK_DOWN_EP_DETECT, HANG_KEY_DEL);
		dhd->hang_info_cnt++;
	}

	/* EWP version */
	if (dhd->hang_info_cnt < HANG_FIELD_CNT_MAX) {
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - bytes_written;
		bytes_written += scnprintf(&dhd->hang_info[bytes_written], remain_len, "%d%c",
				VENDOR_SEND_HANG_EXT_INFO_VER, HANG_KEY_DEL);
		dhd->hang_info_cnt++;
	}

	/* cookie - dump time stamp */
	if (dhd->hang_info_cnt < HANG_FIELD_CNT_MAX) {
		remain_len = VENDOR_SEND_HANG_EXT_INFO_LEN - bytes_written;
		bytes_written += scnprintf(&dhd->hang_info[bytes_written], remain_len, "%s%c",
				dhd->debug_dump_time_hang_str, HANG_KEY_DEL);
		dhd->hang_info_cnt++;
	}

	clear_debug_dump_time(dhd->debug_dump_time_hang_str);

	/* dump PCIE RC registers */
	dhd_dump_pcie_rc_regs_for_linkdown(dhd, &bytes_written);

	DHD_INFO(("hang info haed cnt: %d len: %d data: %s\n",
		dhd->hang_info_cnt, (int)strlen(dhd->hang_info), dhd->hang_info));

}
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */

int
dhd_prot_debug_info_print(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring;
	uint16 rd, wr, drd, dwr;
	uint32 dma_buf_len;
	uint64 current_time;
	ulong ring_tcm_rd_addr; /* dongle address */
	ulong ring_tcm_wr_addr; /* dongle address */

	DHD_ERROR(("\n ------- DUMPING VERSION INFORMATION ------- \r\n"));
	DHD_ERROR(("DHD: %s\n", dhd_version));
	DHD_ERROR(("Firmware: %s\n", fw_version));

#ifdef DHD_FW_COREDUMP
	DHD_ERROR(("\n ------- DUMPING CONFIGURATION INFORMATION ------ \r\n"));
	DHD_ERROR(("memdump mode: %d\n", dhd->memdump_enabled));
#endif /* DHD_FW_COREDUMP */

	DHD_ERROR(("\n ------- DUMPING PROTOCOL INFORMATION ------- \r\n"));
	DHD_ERROR(("ICPrevs: Dev %d, Host %d, active %d\n",
		prot->device_ipc_version,
		prot->host_ipc_version,
		prot->active_ipc_version));
	DHD_ERROR(("d2h_intr_method -> %s\n",
			dhd->bus->d2h_intr_method ? "PCIE_MSI" : "PCIE_INTX"));
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
#if defined(DHD_HTPUT_TUNABLES)
	DHD_ERROR(("h2d_max_txpost: %d, prot->h2d_max_txpost: %d\n",
		h2d_htput_max_txpost, prot->h2d_htput_max_txpost));
#endif /* DHD_HTPUT_TUNABLES */

	current_time = OSL_LOCALTIME_NS();
	DHD_ERROR(("current_time="SEC_USEC_FMT"\n", GET_SEC_USEC(current_time)));
	DHD_ERROR(("ioctl_fillup_time="SEC_USEC_FMT
		" ioctl_ack_time="SEC_USEC_FMT
		" ioctl_cmplt_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(prot->ioctl_fillup_time),
		GET_SEC_USEC(prot->ioctl_ack_time),
		GET_SEC_USEC(prot->ioctl_cmplt_time)));

	/* Check PCIe INT registers */
	if (!dhd_pcie_dump_int_regs(dhd)) {
		DHD_ERROR(("%s : PCIe link might be down\n", __FUNCTION__));
		dhd->bus->is_linkdown = TRUE;
	}

	DHD_ERROR(("\n ------- DUMPING IOCTL RING RD WR Pointers ------- \r\n"));

	ring = &prot->h2dring_ctrl_subn;
	dma_buf_len = ring->max_items * ring->item_len;
	ring_tcm_rd_addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
	ring_tcm_wr_addr = dhd->bus->ring_sh[ring->idx].ring_state_w;
	DHD_ERROR(("CtrlPost: Mem Info: BASE(VA) %p BASE(PA) %x:%x tcm_rd_wr 0x%lx:0x%lx "
		"SIZE %d \r\n",
		ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
		ltoh32(ring->base_addr.low_addr), ring_tcm_rd_addr, ring_tcm_wr_addr, dma_buf_len));
	DHD_ERROR(("CtrlPost: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
	if (dhd->dma_d2h_ring_upd_support) {
		drd = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, ring->idx);
		dwr = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_WR_UPD, ring->idx);
		DHD_ERROR(("CtrlPost: From Host DMA mem: RD: %d WR %d \r\n", drd, dwr));
	}
	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("CtrlPost: From Shared Mem: RD and WR are invalid"
			" due to PCIe link down\r\n"));
	} else {
		dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
		dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
		DHD_ERROR(("CtrlPost: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
	}
	DHD_ERROR(("CtrlPost: seq num: %d \r\n", ring->seqnum % H2D_EPOCH_MODULO));

	ring = &prot->d2hring_ctrl_cpln;
	dma_buf_len = ring->max_items * ring->item_len;
	ring_tcm_rd_addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
	ring_tcm_wr_addr = dhd->bus->ring_sh[ring->idx].ring_state_w;
	DHD_ERROR(("CtrlCpl: Mem Info: BASE(VA) %p BASE(PA) %x:%x tcm_rd_wr 0x%lx:0x%lx "
		"SIZE %d \r\n",
		ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
		ltoh32(ring->base_addr.low_addr), ring_tcm_rd_addr, ring_tcm_wr_addr, dma_buf_len));
	DHD_ERROR(("CtrlCpl: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
	if (dhd->dma_d2h_ring_upd_support) {
		drd = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
		dwr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
		DHD_ERROR(("CtrlCpl: From Host DMA mem: RD: %d WR %d \r\n", drd, dwr));
	}
	if (dhd->bus->is_linkdown) {
		DHD_ERROR(("CtrlCpl: From Shared Mem: RD and WR are invalid"
			" due to PCIe link down\r\n"));
	} else {
		dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
		dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
		DHD_ERROR(("CtrlCpl: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
	}
	DHD_ERROR(("CtrlCpl: Expected seq num: %d \r\n", ring->seqnum % H2D_EPOCH_MODULO));

	ring = prot->h2dring_info_subn;
	if (ring) {
		dma_buf_len = ring->max_items * ring->item_len;
		ring_tcm_rd_addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
		ring_tcm_wr_addr = dhd->bus->ring_sh[ring->idx].ring_state_w;
		DHD_ERROR(("InfoSub: Mem Info: BASE(VA) %p BASE(PA) %x:%x tcm_rd_wr 0x%lx:0x%lx "
			"SIZE %d \r\n",
			ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
			ltoh32(ring->base_addr.low_addr), ring_tcm_rd_addr, ring_tcm_wr_addr,
			dma_buf_len));
		DHD_ERROR(("InfoSub: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
		if (dhd->dma_d2h_ring_upd_support) {
			drd = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_WR_UPD, ring->idx);
			DHD_ERROR(("InfoSub: From Host DMA mem: RD: %d WR %d \r\n", drd, dwr));
		}
		if (dhd->bus->is_linkdown) {
			DHD_ERROR(("InfoSub: From Shared Mem: RD and WR are invalid"
				" due to PCIe link down\r\n"));
		} else {
			dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
			dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
			DHD_ERROR(("InfoSub: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
		}
		DHD_ERROR(("InfoSub: seq num: %d \r\n", ring->seqnum % H2D_EPOCH_MODULO));
	}
	ring = prot->d2hring_info_cpln;
	if (ring) {
		dma_buf_len = ring->max_items * ring->item_len;
		ring_tcm_rd_addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
		ring_tcm_wr_addr = dhd->bus->ring_sh[ring->idx].ring_state_w;
		DHD_ERROR(("InfoCpl: Mem Info: BASE(VA) %p BASE(PA) %x:%x tcm_rd_wr 0x%lx:0x%lx "
			"SIZE %d \r\n",
			ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
			ltoh32(ring->base_addr.low_addr), ring_tcm_rd_addr, ring_tcm_wr_addr,
			dma_buf_len));
		DHD_ERROR(("InfoCpl: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
		if (dhd->dma_d2h_ring_upd_support) {
			drd = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
			DHD_ERROR(("InfoCpl: From Host DMA mem: RD: %d WR %d \r\n", drd, dwr));
		}
		if (dhd->bus->is_linkdown) {
			DHD_ERROR(("InfoCpl: From Shared Mem: RD and WR are invalid"
				" due to PCIe link down\r\n"));
		} else {
			dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
			dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
			DHD_ERROR(("InfoCpl: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
		}
		DHD_ERROR(("InfoCpl: Expected seq num: %d \r\n", ring->seqnum % D2H_EPOCH_MODULO));
	}
#ifdef EWP_EDL
	ring = prot->d2hring_edl;
	if (ring) {
		ring_tcm_rd_addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
		ring_tcm_wr_addr = dhd->bus->ring_sh[ring->idx].ring_state_w;
		dma_buf_len = ring->max_items * ring->item_len;
		DHD_ERROR(("EdlRing: Mem Info: BASE(VA) %p BASE(PA) %x:%x tcm_rd_wr 0x%lx:0x%lx "
			"SIZE %d \r\n",
			ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
			ltoh32(ring->base_addr.low_addr), ring_tcm_rd_addr, ring_tcm_wr_addr,
			dma_buf_len));
		DHD_ERROR(("EdlRing: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
		if (dhd->dma_d2h_ring_upd_support) {
			drd = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
			DHD_ERROR(("EdlRing: From Host DMA mem: RD: %d WR %d \r\n", drd, dwr));
		}
		if (dhd->bus->is_linkdown) {
			DHD_ERROR(("EdlRing: From Shared Mem: RD and WR are invalid"
				" due to PCIe link down\r\n"));
		} else {
			dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
			dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
			DHD_ERROR(("EdlRing: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
		}
		DHD_ERROR(("EdlRing: Expected seq num: %d \r\n",
			ring->seqnum % D2H_EPOCH_MODULO));
	}
#endif /* EWP_EDL */

	ring = &prot->d2hring_tx_cpln;
	if (ring) {
		ring_tcm_rd_addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
		ring_tcm_wr_addr = dhd->bus->ring_sh[ring->idx].ring_state_w;
		dma_buf_len = ring->max_items * ring->item_len;
		DHD_ERROR(("TxCpl: Mem Info: BASE(VA) %p BASE(PA) %x:%x tcm_rd_wr 0x%lx:0x%lx "
			"SIZE %d \r\n",
			ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
			ltoh32(ring->base_addr.low_addr), ring_tcm_rd_addr, ring_tcm_wr_addr,
			dma_buf_len));
		DHD_ERROR(("TxCpl: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
		if (dhd->dma_d2h_ring_upd_support) {
			drd = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
			DHD_ERROR(("TxCpl: From Host DMA mem: RD: %d WR %d \r\n", drd, dwr));
		}
		if (dhd->bus->is_linkdown) {
			DHD_ERROR(("TxCpl: From Shared Mem: RD and WR are invalid"
				" due to PCIe link down\r\n"));
		} else {
			dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
			dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
			DHD_ERROR(("TxCpl: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
		}
		DHD_ERROR(("TxCpl: Expected seq num: %d \r\n", ring->seqnum % D2H_EPOCH_MODULO));
	}

	ring = &prot->d2hring_rx_cpln;
	if (ring) {
		ring_tcm_rd_addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
		ring_tcm_wr_addr = dhd->bus->ring_sh[ring->idx].ring_state_w;
		dma_buf_len = ring->max_items * ring->item_len;
		DHD_ERROR(("RxCpl: Mem Info: BASE(VA) %p BASE(PA) %x:%x tcm_rd_wr 0x%lx:0x%lx "
			"SIZE %d \r\n",
			ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
			ltoh32(ring->base_addr.low_addr), ring_tcm_rd_addr, ring_tcm_wr_addr,
			dma_buf_len));
		DHD_ERROR(("RxCpl: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
		if (dhd->dma_d2h_ring_upd_support) {
			drd = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
			DHD_ERROR(("RxCpl: From Host DMA mem: RD: %d WR %d \r\n", drd, dwr));
		}
		if (dhd->bus->is_linkdown) {
			DHD_ERROR(("RxCpl: From Shared Mem: RD and WR are invalid"
				" due to PCIe link down\r\n"));
		} else {
			dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
			dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
			DHD_ERROR(("RxCpl: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
		}
		DHD_ERROR(("RxCpl: Expected seq num: %d \r\n", ring->seqnum % D2H_EPOCH_MODULO));
	}

	ring = &prot->h2dring_rxp_subn;
	if (ring) {
		ring_tcm_rd_addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
		ring_tcm_wr_addr = dhd->bus->ring_sh[ring->idx].ring_state_w;
		dma_buf_len = ring->max_items * ring->item_len;
		DHD_ERROR(("RxSub: Mem Info: BASE(VA) %p BASE(PA) %x:%x tcm_rd_wr 0x%lx:0x%lx "
			"SIZE %d \r\n",
			ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
			ltoh32(ring->base_addr.low_addr), ring_tcm_rd_addr, ring_tcm_wr_addr,
			dma_buf_len));
		DHD_ERROR(("RxSub: From Host mem: RD: %d WR %d \r\n", ring->rd, ring->wr));
		if (dhd->dma_d2h_ring_upd_support) {
			drd = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_WR_UPD, ring->idx);
			DHD_ERROR(("RxSub: From Host DMA mem: RD: %d WR %d \r\n", drd, dwr));
		}
		if (dhd->bus->is_linkdown) {
			DHD_ERROR(("RxSub: From Shared Mem: RD and WR are invalid"
				" due to PCIe link down\r\n"));
		} else {
			dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
			dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
			DHD_ERROR(("RxSub: From Shared Mem: RD: %d WR %d \r\n", rd, wr));
		}
		DHD_ERROR(("RxSub: Expected seq num: %d \r\n", ring->seqnum % D2H_EPOCH_MODULO));
	}

	DHD_ERROR(("%s: cur_ioctlresp_bufs_posted %d cur_event_bufs_posted %d\n",
		__FUNCTION__, prot->cur_ioctlresp_bufs_posted, prot->cur_event_bufs_posted));
#ifdef DHD_LIMIT_MULTI_CLIENT_FLOWRINGS
	DHD_ERROR(("%s: multi_client_flow_rings:%d max_multi_client_flow_rings:%d\n",
		__FUNCTION__, dhd->multi_client_flow_rings, dhd->max_multi_client_flow_rings));
#endif /* DHD_LIMIT_MULTI_CLIENT_FLOWRINGS */

	DHD_ERROR(("pktid_txq_start_cnt: %d\n", prot->pktid_txq_start_cnt));
	DHD_ERROR(("pktid_txq_stop_cnt: %d\n", prot->pktid_txq_stop_cnt));
	DHD_ERROR(("pktid_depleted_cnt: %d\n", prot->pktid_depleted_cnt));
	dhd_pcie_debug_info_dump(dhd);
#ifdef DHD_LB_STATS
	DHD_ERROR(("\nlb_rxp_stop_thr_hitcnt: %llu lb_rxp_strt_thr_hitcnt: %llu\n",
		dhd->lb_rxp_stop_thr_hitcnt, dhd->lb_rxp_strt_thr_hitcnt));
	DHD_ERROR(("\nlb_rxp_napi_sched_cnt: %llu lb_rxp_napi_complete_cnt: %llu\n",
		dhd->lb_rxp_napi_sched_cnt, dhd->lb_rxp_napi_complete_cnt));
#endif /* DHD_LB_STATS */
#ifdef DHD_TIMESYNC
	dhd_timesync_debug_info_print(dhd);
#endif /* DHD_TIMESYNC */
	return 0;
}

int
dhd_prot_ringupd_dump(dhd_pub_t *dhd, struct bcmstrbuf *b)
{
	uint32 *ptr;
	uint32 value;

	if (dhd->prot->d2h_dma_indx_wr_buf.va) {
		uint32 i;
		uint32 max_h2d_queues = dhd_bus_max_h2d_queues(dhd->bus);

		OSL_CACHE_INV((void *)dhd->prot->d2h_dma_indx_wr_buf.va,
			dhd->prot->d2h_dma_indx_wr_buf.len);

		ptr = (uint32 *)(dhd->prot->d2h_dma_indx_wr_buf.va);

		bcm_bprintf(b, "\n max_tx_queues %d\n", max_h2d_queues);

		bcm_bprintf(b, "\nRPTR block H2D common rings, 0x%4p\n", ptr);
		value = ltoh32(*ptr);
		bcm_bprintf(b, "\tH2D CTRL: value 0x%04x\n", value);
		ptr++;
		value = ltoh32(*ptr);
		bcm_bprintf(b, "\tH2D RXPOST: value 0x%04x\n", value);

		ptr++;
		bcm_bprintf(b, "RPTR block Flow rings , 0x%4p\n", ptr);
		for (i = BCMPCIE_H2D_COMMON_MSGRINGS; i < max_h2d_queues; i++) {
			value = ltoh32(*ptr);
			bcm_bprintf(b, "\tflowring ID %d: value 0x%04x\n", i, value);
			ptr++;
		}
	}

	if (dhd->prot->h2d_dma_indx_rd_buf.va) {
		OSL_CACHE_INV((void *)dhd->prot->h2d_dma_indx_rd_buf.va,
			dhd->prot->h2d_dma_indx_rd_buf.len);

		ptr = (uint32 *)(dhd->prot->h2d_dma_indx_rd_buf.va);

		bcm_bprintf(b, "\nWPTR block D2H common rings, 0x%4p\n", ptr);
		value = ltoh32(*ptr);
		bcm_bprintf(b, "\tD2H CTRLCPLT: value 0x%04x\n", value);
		ptr++;
		value = ltoh32(*ptr);
		bcm_bprintf(b, "\tD2H TXCPLT: value 0x%04x\n", value);
		ptr++;
		value = ltoh32(*ptr);
		bcm_bprintf(b, "\tD2H RXCPLT: value 0x%04x\n", value);
	}

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
#if !(defined(BCM_ROUTER_DHD))
	dhd_prot_t *prot = dhd->prot;
	if (rx)
		prot->rx_metadata_offset = (uint16)val;
	else
		prot->tx_metadata_offset = (uint16)val;
#endif /* ! BCM_ROUTER_DHD */
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

static INLINE void
BCMFASTPATH(dhd_rxchain_reset)(rxchain_info_t *rxchain)
{
	rxchain->pkt_count = 0;
}

static void
BCMFASTPATH(dhd_rxchain_frame)(dhd_pub_t *dhd, void *pkt, uint ifidx)
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

static void
BCMFASTPATH(dhd_rxchain_commit)(dhd_pub_t *dhd)
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

	DHD_RING_LOCK(ctrl_ring->ring_lock, flags);

	/* Request for ctrl_ring buffer space */
	flow_resume_rqst = (tx_idle_flowring_resume_request_t *)
		dhd_prot_alloc_ring_space(dhd, ctrl_ring, 1, &alloced, FALSE);

	if (flow_resume_rqst == NULL) {
		dhd_prot_flowrings_pool_release(dhd, flow_ring_node->flowid, flow_ring);
		DHD_ERROR(("%s: Flow resume Req flowid %d - failure ring space\n",
			__FUNCTION__, flow_ring_node->flowid));
		DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);
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

	DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

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

	DHD_RING_LOCK(ring->ring_lock, flags);

	/* Request for ring buffer space */
	flow_suspend_rqst = (tx_idle_flowring_suspend_request_t *)
		dhd_prot_alloc_ring_space(dhd, ring, 1, &alloced, FALSE);

	if (flow_suspend_rqst == NULL) {
		DHD_RING_UNLOCK(ring->ring_lock, flags);
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

	DHD_RING_UNLOCK(ring->ring_lock, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhd->bus);
#endif

	return BCME_OK;
}
#endif /* IDLE_TX_FLOW_MGMT */

#if defined(BCMINTERNAL) && defined(DHD_DBG_DUMP)
static void
dhd_prot_ioctl_trace(dhd_pub_t *dhd, ioctl_req_msg_t *ioct_rqst, uchar *buf, int len)
{
	struct dhd_prot *prot = dhd->prot;
	uint32 cnt = prot->ioctl_trace_count % MAX_IOCTL_TRACE_SIZE;

	prot->ioctl_trace[cnt].cmd = ioct_rqst->cmd;
	prot->ioctl_trace[cnt].transid = ioct_rqst->trans_id;
	if ((ioct_rqst->cmd == 262 || ioct_rqst->cmd == 263) && buf)
		memcpy(prot->ioctl_trace[cnt].ioctl_buf, buf,
			len > MAX_IOCTL_BUF_SIZE ? MAX_IOCTL_BUF_SIZE : len);
	else
		memset(prot->ioctl_trace[cnt].ioctl_buf, 0, MAX_IOCTL_BUF_SIZE);
	prot->ioctl_trace[cnt].timestamp = OSL_SYSUPTIME_US();
	prot->ioctl_trace_count ++;
}

static void
dhd_prot_ioctl_dump(dhd_prot_t *prot, struct bcmstrbuf *strbuf)
{
	int dumpsz;
	int i;

	dumpsz = prot->ioctl_trace_count < MAX_IOCTL_TRACE_SIZE ?
		prot->ioctl_trace_count : MAX_IOCTL_TRACE_SIZE;
	if (dumpsz == 0) {
		bcm_bprintf(strbuf, "\nEmpty IOCTL TRACE\n");
		return;
	}
	bcm_bprintf(strbuf, "----------- IOCTL TRACE --------------\n");
	bcm_bprintf(strbuf, "Timestamp us\t\tCMD\tTransID\tIOVAR\n");
	for (i = 0; i < dumpsz; i ++) {
		bcm_bprintf(strbuf, "%llu\t%d\t%d\t%s\n",
			prot->ioctl_trace[i].timestamp,
			prot->ioctl_trace[i].cmd,
			prot->ioctl_trace[i].transid,
			prot->ioctl_trace[i].ioctl_buf);
	}
}
#endif /* defined(BCMINTERNAL) && defined(DHD_DBG_DUMP) */

static void dump_psmwd_v1(const bcm_tlv_t *tlv, struct bcmstrbuf *b)
{
	const hnd_ext_trap_psmwd_v1_t* psmwd = NULL;
	uint32 i;
	psmwd = (const hnd_ext_trap_psmwd_v1_t *)tlv;
	for (i = 0; i < PSMDBG_REG_READ_CNT_FOR_PSMWDTRAP_V1; i++) {
		bcm_bprintf(b, " psmdebug[%d]: 0x%x\n", i, psmwd->i32_psmdebug[i]);
	}
	bcm_bprintf(b, " gated clock en: 0x%x\n", psmwd->i16_0x1a8);
	bcm_bprintf(b, " Rcv Fifo Ctrl: 0x%x\n", psmwd->i16_0x406);
	bcm_bprintf(b, " Rx ctrl 1: 0x%x\n", psmwd->i16_0x408);
	bcm_bprintf(b, " Rxe Status 1: 0x%x\n", psmwd->i16_0x41a);
	bcm_bprintf(b, " Rxe Status 2: 0x%x\n", psmwd->i16_0x41c);
	bcm_bprintf(b, " rcv wrd count 0: 0x%x\n", psmwd->i16_0x424);
	bcm_bprintf(b, " rcv wrd count 1: 0x%x\n", psmwd->i16_0x426);
	bcm_bprintf(b, " RCV_LFIFO_STS: 0x%x\n", psmwd->i16_0x456);
	bcm_bprintf(b, " PSM_SLP_TMR: 0x%x\n", psmwd->i16_0x480);
	bcm_bprintf(b, " TXE CTRL: 0x%x\n", psmwd->i16_0x500);
	bcm_bprintf(b, " TXE Status: 0x%x\n", psmwd->i16_0x50e);
	bcm_bprintf(b, " TXE_xmtdmabusy: 0x%x\n", psmwd->i16_0x55e);
	bcm_bprintf(b, " TXE_XMTfifosuspflush: 0x%x\n", psmwd->i16_0x566);
	bcm_bprintf(b, " IFS Stat: 0x%x\n", psmwd->i16_0x690);
	bcm_bprintf(b, " IFS_MEDBUSY_CTR: 0x%x\n", psmwd->i16_0x692);
	bcm_bprintf(b, " IFS_TX_DUR: 0x%x\n", psmwd->i16_0x694);
	bcm_bprintf(b, " SLow_CTL: 0x%x\n", psmwd->i16_0x6a0);
	bcm_bprintf(b, " PSM BRC: 0x%x\n", psmwd->i16_0x490);
	bcm_bprintf(b, " TXE_AQM fifo Ready: 0x%x\n", psmwd->i16_0x838);
	bcm_bprintf(b, " Dagg ctrl: 0x%x\n", psmwd->i16_0x8c0);
	bcm_bprintf(b, " shm_prewds_cnt: 0x%x\n", psmwd->shm_prewds_cnt);
	bcm_bprintf(b, " shm_txtplufl_cnt: 0x%x\n", psmwd->shm_txtplufl_cnt);
	bcm_bprintf(b, " shm_txphyerr_cnt: 0x%x\n", psmwd->shm_txphyerr_cnt);

}

static void dump_psmwd_v2(const bcm_tlv_t *tlv, struct bcmstrbuf *b)
{
	const hnd_ext_trap_psmwd_t* psmwd = NULL;
	uint32 i;
	psmwd = (const hnd_ext_trap_psmwd_t *)tlv;
	for (i = 0; i < PSMDBG_REG_READ_CNT_FOR_PSMWDTRAP_V2; i++) {
		bcm_bprintf(b, " psmdebug[%d]: 0x%x\n", i, psmwd->i32_psmdebug[i]);
	}

	bcm_bprintf(b, " psm_brwk0: 0x%x\n", psmwd->i16_0x4b8);
	bcm_bprintf(b, " psm_brwk1: 0x%x\n", psmwd->i16_0x4ba);
	bcm_bprintf(b, " psm_brwk2: 0x%x\n", psmwd->i16_0x4bc);
	bcm_bprintf(b, " psm_brwk3: 0x%x\n", psmwd->i16_0x4be);
	bcm_bprintf(b, " PSM BRC_1: 0x%x\n", psmwd->i16_0x4da);
	bcm_bprintf(b, " gated clock en: 0x%x\n", psmwd->i16_0x1a8);
	bcm_bprintf(b, " Rcv Fifo Ctrl: 0x%x\n", psmwd->i16_0x406);
	bcm_bprintf(b, " Rx ctrl 1: 0x%x\n", psmwd->i16_0x408);
	bcm_bprintf(b, " Rxe Status 1: 0x%x\n", psmwd->i16_0x41a);
	bcm_bprintf(b, " Rxe Status 2: 0x%x\n", psmwd->i16_0x41c);
	bcm_bprintf(b, " rcv wrd count 0: 0x%x\n", psmwd->i16_0x424);
	bcm_bprintf(b, " rcv wrd count 1: 0x%x\n", psmwd->i16_0x426);
	bcm_bprintf(b, " RCV_LFIFO_STS: 0x%x\n", psmwd->i16_0x456);
	bcm_bprintf(b, " PSM_SLP_TMR: 0x%x\n", psmwd->i16_0x480);
	bcm_bprintf(b, " TXE CTRL: 0x%x\n", psmwd->i16_0x500);
	bcm_bprintf(b, " TXE Status: 0x%x\n", psmwd->i16_0x50e);
	bcm_bprintf(b, " TXE_xmtdmabusy: 0x%x\n", psmwd->i16_0x55e);
	bcm_bprintf(b, " TXE_XMTfifosuspflush: 0x%x\n", psmwd->i16_0x566);
	bcm_bprintf(b, " IFS Stat: 0x%x\n", psmwd->i16_0x690);
	bcm_bprintf(b, " IFS_MEDBUSY_CTR: 0x%x\n", psmwd->i16_0x692);
	bcm_bprintf(b, " IFS_TX_DUR: 0x%x\n", psmwd->i16_0x694);
	bcm_bprintf(b, " SLow_CTL: 0x%x\n", psmwd->i16_0x6a0);
	bcm_bprintf(b, " PSM BRC: 0x%x\n", psmwd->i16_0x490);
	bcm_bprintf(b, " TXE_AQM fifo Ready: 0x%x\n", psmwd->i16_0x838);
	bcm_bprintf(b, " Dagg ctrl: 0x%x\n", psmwd->i16_0x8c0);
	bcm_bprintf(b, " shm_prewds_cnt: 0x%x\n", psmwd->shm_prewds_cnt);
	bcm_bprintf(b, " shm_txtplufl_cnt: 0x%x\n", psmwd->shm_txtplufl_cnt);
	bcm_bprintf(b, " shm_txphyerr_cnt: 0x%x\n", psmwd->shm_txphyerr_cnt);
}

static const char* etd_trap_name(hnd_ext_tag_trap_t tag)
{
	switch (tag) {
	case TAG_TRAP_SIGNATURE: return "TAG_TRAP_SIGNATURE";
	case TAG_TRAP_STACK: return "TAG_TRAP_STACK";
	case TAG_TRAP_MEMORY: return "TAG_TRAP_MEMORY";
	case TAG_TRAP_DEEPSLEEP: return "TAG_TRAP_DEEPSLEEP";
	case TAG_TRAP_PSM_WD: return "TAG_TRAP_PSM_WD";
	case TAG_TRAP_PHY: return "TAG_TRAP_PHY";
	case TAG_TRAP_BUS: return "TAG_TRAP_BUS";
	case TAG_TRAP_MAC_SUSP: return "TAG_TRAP_MAC_SUSP";
	case TAG_TRAP_BACKPLANE: return "TAG_TRAP_BACKPLANE";
	case TAG_TRAP_PCIE_Q: return "TAG_TRAP_PCIE_Q";
	case TAG_TRAP_WLC_STATE: return "TAG_TRAP_WLC_STATE";
	case TAG_TRAP_MAC_WAKE: return "TAG_TRAP_MAC_WAKE";
	case TAG_TRAP_HMAP: return "TAG_TRAP_HMAP";
	case TAG_TRAP_PHYTXERR_THRESH: return "TAG_TRAP_PHYTXERR_THRESH";
	case TAG_TRAP_HC_DATA: return "TAG_TRAP_HC_DATA";
	case TAG_TRAP_LOG_DATA: return "TAG_TRAP_LOG_DATA";
	case TAG_TRAP_CODE: return "TAG_TRAP_CODE";
	case TAG_TRAP_MEM_BIT_FLIP: return "TAG_TRAP_MEM_BIT_FLIP";
	case TAG_TRAP_LAST:
	default:
		return "Unknown";
	}
	return "Unknown";
}

int dhd_prot_dump_extended_trap(dhd_pub_t *dhdp, struct bcmstrbuf *b, bool raw)
{
	uint32 i;
	uint32 *ext_data;
	hnd_ext_trap_hdr_t *hdr;
	const bcm_tlv_t *tlv;
	const trap_t *tr;
	const uint32 *stack;
	const hnd_ext_trap_bp_err_t *bpe;
	uint32 raw_len;

	ext_data = dhdp->extended_trap_data;

	/* return if there is no extended trap data */
	if (!ext_data || !(dhdp->dongle_trap_data & D2H_DEV_EXT_TRAP_DATA)) {
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

	/* Dump a list of all tags found  before parsing data */
	bcm_bprintf(b, "\nTags Found:\n");
	for (i = 0; i < TAG_TRAP_LAST; i++) {
		tlv = bcm_parse_tlvs(hdr->data, hdr->len, i);
		if (tlv)
			bcm_bprintf(b, "Tag: %d (%s), Length: %d\n", i, etd_trap_name(i), tlv->len);
	}

	/* XXX debug dump */
	if (raw) {
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
	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_CODE);
	if (tlv) {
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_CODE), tlv->len);
		bcm_bprintf(b, "ETD TYPE: %d\n", tlv->data[0]);
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_SIGNATURE);
	if (tlv) {
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_SIGNATURE), tlv->len);
		tr = (const trap_t *)tlv->data;

		bcm_bprintf(b, "TRAP %x: pc %x, lr %x, sp %x, cpsr %x, spsr %x\n",
		       tr->type, tr->pc, tr->r14, tr->r13, tr->cpsr, tr->spsr);
		bcm_bprintf(b, "  r0 %x, r1 %x, r2 %x, r3 %x, r4 %x, r5 %x, r6 %x\n",
		       tr->r0, tr->r1, tr->r2, tr->r3, tr->r4, tr->r5, tr->r6);
		bcm_bprintf(b, "  r7 %x, r8 %x, r9 %x, r10 %x, r11 %x, r12 %x\n",
		       tr->r7, tr->r8, tr->r9, tr->r10, tr->r11, tr->r12);
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_STACK);
	if (tlv) {
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_STACK), tlv->len);
		stack = (const uint32 *)tlv->data;
		for (i = 0; i < (uint32)(tlv->len / 4); i++)
		{
			bcm_bprintf(b, "  0x%08x\n", *stack);
			stack++;
		}
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_BACKPLANE);
	if (tlv) {
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_BACKPLANE), tlv->len);
		bpe = (const hnd_ext_trap_bp_err_t *)tlv->data;
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

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_MEMORY);
	if (tlv) {
		const hnd_ext_trap_heap_err_t* hme;

		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_MEMORY), tlv->len);
		hme = (const hnd_ext_trap_heap_err_t *)tlv->data;
		bcm_bprintf(b, " arena total: %d\n", hme->arena_total);
		bcm_bprintf(b, " heap free: %d\n", hme->heap_free);
		bcm_bprintf(b, " heap in use: %d\n", hme->heap_inuse);
		bcm_bprintf(b, " mf count: %d\n", hme->mf_count);
		bcm_bprintf(b, " stack LWM: %x\n", hme->stack_lwm);

		bcm_bprintf(b, " Histogram:\n");
		for (i = 0; i < (HEAP_HISTOGRAM_DUMP_LEN * 2); i += 2) {
			if (hme->heap_histogm[i] == 0xfffe)
				bcm_bprintf(b, " Others\t%d\t?\n", hme->heap_histogm[i + 1]);
			else if (hme->heap_histogm[i] == 0xffff)
				bcm_bprintf(b, " >= 256K\t%d\t?\n", hme->heap_histogm[i + 1]);
			else
				bcm_bprintf(b, " %d\t%d\t%d\n", hme->heap_histogm[i] << 2,
					hme->heap_histogm[i + 1], (hme->heap_histogm[i] << 2)
					* hme->heap_histogm[i + 1]);
		}

		bcm_bprintf(b, " Max free block: %d\n", hme->max_sz_free_blk[0] << 2);
		for (i = 1; i < HEAP_MAX_SZ_BLKS_LEN; i++) {
			bcm_bprintf(b, " Next lgst free block: %d\n", hme->max_sz_free_blk[i] << 2);
		}
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_PCIE_Q);
	if (tlv) {
		const hnd_ext_trap_pcie_mem_err_t* pqme;

		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_PCIE_Q), tlv->len);
		pqme = (const hnd_ext_trap_pcie_mem_err_t *)tlv->data;
		bcm_bprintf(b, " d2h queue len: %x\n", pqme->d2h_queue_len);
		bcm_bprintf(b, " d2h req queue len: %x\n", pqme->d2h_req_queue_len);
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_WLC_STATE);
	if (tlv) {
		const hnd_ext_trap_wlc_mem_err_t* wsme;

		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_WLC_STATE), tlv->len);
		wsme = (const hnd_ext_trap_wlc_mem_err_t *)tlv->data;
		bcm_bprintf(b, " instance: %d\n", wsme->instance);
		bcm_bprintf(b, " associated: %d\n", wsme->associated);
		bcm_bprintf(b, " peer count: %d\n", wsme->peer_cnt);
		bcm_bprintf(b, " client count: %d\n", wsme->soft_ap_client_cnt);
		bcm_bprintf(b, " TX_AC_BK_FIFO: %d\n", wsme->txqueue_len[0]);
		bcm_bprintf(b, " TX_AC_BE_FIFO: %d\n", wsme->txqueue_len[1]);
		bcm_bprintf(b, " TX_AC_VI_FIFO: %d\n", wsme->txqueue_len[2]);
		bcm_bprintf(b, " TX_AC_VO_FIFO: %d\n", wsme->txqueue_len[3]);

		if (tlv->len >= (sizeof(*wsme) * 2)) {
			wsme++;
			bcm_bprintf(b, "\n instance: %d\n", wsme->instance);
			bcm_bprintf(b, " associated: %d\n", wsme->associated);
			bcm_bprintf(b, " peer count: %d\n", wsme->peer_cnt);
			bcm_bprintf(b, " client count: %d\n", wsme->soft_ap_client_cnt);
			bcm_bprintf(b, " TX_AC_BK_FIFO: %d\n", wsme->txqueue_len[0]);
			bcm_bprintf(b, " TX_AC_BE_FIFO: %d\n", wsme->txqueue_len[1]);
			bcm_bprintf(b, " TX_AC_VI_FIFO: %d\n", wsme->txqueue_len[2]);
			bcm_bprintf(b, " TX_AC_VO_FIFO: %d\n", wsme->txqueue_len[3]);
		}
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_PHY);
	if (tlv) {
		const hnd_ext_trap_phydbg_t* phydbg;
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_PHY), tlv->len);
		phydbg = (const hnd_ext_trap_phydbg_t *)tlv->data;
		bcm_bprintf(b, " err: 0x%x\n", phydbg->err);
		bcm_bprintf(b, " RxFeStatus: 0x%x\n", phydbg->RxFeStatus);
		bcm_bprintf(b, " TxFIFOStatus0: 0x%x\n", phydbg->TxFIFOStatus0);
		bcm_bprintf(b, " TxFIFOStatus1: 0x%x\n", phydbg->TxFIFOStatus1);
		bcm_bprintf(b, " RfseqMode: 0x%x\n", phydbg->RfseqMode);
		bcm_bprintf(b, " RfseqStatus0: 0x%x\n", phydbg->RfseqStatus0);
		bcm_bprintf(b, " RfseqStatus1: 0x%x\n", phydbg->RfseqStatus1);
		bcm_bprintf(b, " RfseqStatus_Ocl: 0x%x\n", phydbg->RfseqStatus_Ocl);
		bcm_bprintf(b, " RfseqStatus_Ocl1: 0x%x\n", phydbg->RfseqStatus_Ocl1);
		bcm_bprintf(b, " OCLControl1: 0x%x\n", phydbg->OCLControl1);
		bcm_bprintf(b, " TxError: 0x%x\n", phydbg->TxError);
		bcm_bprintf(b, " bphyTxError: 0x%x\n", phydbg->bphyTxError);
		bcm_bprintf(b, " TxCCKError: 0x%x\n", phydbg->TxCCKError);
		bcm_bprintf(b, " TxCtrlWrd0: 0x%x\n", phydbg->TxCtrlWrd0);
		bcm_bprintf(b, " TxCtrlWrd1: 0x%x\n", phydbg->TxCtrlWrd1);
		bcm_bprintf(b, " TxCtrlWrd2: 0x%x\n", phydbg->TxCtrlWrd2);
		bcm_bprintf(b, " TxLsig0: 0x%x\n", phydbg->TxLsig0);
		bcm_bprintf(b, " TxLsig1: 0x%x\n", phydbg->TxLsig1);
		bcm_bprintf(b, " TxVhtSigA10: 0x%x\n", phydbg->TxVhtSigA10);
		bcm_bprintf(b, " TxVhtSigA11: 0x%x\n", phydbg->TxVhtSigA11);
		bcm_bprintf(b, " TxVhtSigA20: 0x%x\n", phydbg->TxVhtSigA20);
		bcm_bprintf(b, " TxVhtSigA21: 0x%x\n", phydbg->TxVhtSigA21);
		bcm_bprintf(b, " txPktLength: 0x%x\n", phydbg->txPktLength);
		bcm_bprintf(b, " txPsdulengthCtr: 0x%x\n", phydbg->txPsdulengthCtr);
		bcm_bprintf(b, " gpioClkControl: 0x%x\n", phydbg->gpioClkControl);
		bcm_bprintf(b, " gpioSel: 0x%x\n", phydbg->gpioSel);
		bcm_bprintf(b, " pktprocdebug: 0x%x\n", phydbg->pktprocdebug);
		for (i = 0; i < 3; i++)
			bcm_bprintf(b, " gpioOut[%d]: 0x%x\n", i, phydbg->gpioOut[i]);
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_PSM_WD);
	if (tlv) {
		const hnd_ext_trap_psmwd_t* psmwd;

		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_PSM_WD), tlv->len);
		psmwd = (const hnd_ext_trap_psmwd_t *)tlv->data;
		bcm_bprintf(b, " version: 0x%x\n", psmwd->version);
		bcm_bprintf(b, " maccontrol: 0x%x\n", psmwd->i32_maccontrol);
		bcm_bprintf(b, " maccommand: 0x%x\n", psmwd->i32_maccommand);
		bcm_bprintf(b, " macintstatus: 0x%x\n", psmwd->i32_macintstatus);
		bcm_bprintf(b, " phydebug: 0x%x\n", psmwd->i32_phydebug);
		bcm_bprintf(b, " clk_ctl_st: 0x%x\n", psmwd->i32_clk_ctl_st);
		if (psmwd->version == 1) {
			dump_psmwd_v1(tlv, b);
		}
		if (psmwd->version == 2) {
			dump_psmwd_v2(tlv, b);
		}
	}
/* PHY TxErr MacDump */
	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_PHYTXERR_THRESH);
	if (tlv) {
		const hnd_ext_trap_macphytxerr_t* phytxerr = NULL;
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_PHYTXERR_THRESH), tlv->len);
		phytxerr = (const hnd_ext_trap_macphytxerr_t *)tlv->data;
		bcm_bprintf(b, " version: 0x%x\n", phytxerr->version);
		bcm_bprintf(b, " trap_reason: %d\n", phytxerr->trap_reason);
		bcm_bprintf(b, " Tsf_rx_ts_0x63E: 0x%x\n", phytxerr->i16_0x63E);
		bcm_bprintf(b, " Tsf_tx_ts_0x640: 0x%x\n", phytxerr->i16_0x640);
		bcm_bprintf(b, " tsf_tmr_rx_end_ts_0x642: 0x%x\n", phytxerr->i16_0x642);
		bcm_bprintf(b, " TDC_FrmLen0_0x846: 0x%x\n", phytxerr->i16_0x846);
		bcm_bprintf(b, " TDC_FrmLen1_0x848: 0x%x\n", phytxerr->i16_0x848);
		bcm_bprintf(b, " TDC_Txtime_0x84a: 0x%x\n", phytxerr->i16_0x84a);
		bcm_bprintf(b, " TXE_BytCntInTxFrmLo_0xa5a: 0x%x\n", phytxerr->i16_0xa5a);
		bcm_bprintf(b, " TXE_BytCntInTxFrmHi_0xa5c: 0x%x\n", phytxerr->i16_0xa5c);
		bcm_bprintf(b, " TDC_VhtPsduLen0_0x856: 0x%x\n", phytxerr->i16_0x856);
		bcm_bprintf(b, " TDC_VhtPsduLen1_0x858: 0x%x\n", phytxerr->i16_0x858);
		bcm_bprintf(b, " PSM_BRC: 0x%x\n", phytxerr->i16_0x490);
		bcm_bprintf(b, " PSM_BRC_1: 0x%x\n", phytxerr->i16_0x4d8);
		bcm_bprintf(b, " shm_txerr_reason: 0x%x\n", phytxerr->shm_txerr_reason);
		bcm_bprintf(b, " shm_pctl0: 0x%x\n", phytxerr->shm_pctl0);
		bcm_bprintf(b, " shm_pctl1: 0x%x\n", phytxerr->shm_pctl1);
		bcm_bprintf(b, " shm_pctl2: 0x%x\n", phytxerr->shm_pctl2);
		bcm_bprintf(b, " shm_lsig0: 0x%x\n", phytxerr->shm_lsig0);
		bcm_bprintf(b, " shm_lsig1: 0x%x\n", phytxerr->shm_lsig1);
		bcm_bprintf(b, " shm_plcp0: 0x%x\n", phytxerr->shm_plcp0);
		bcm_bprintf(b, " shm_plcp1: 0x%x\n", phytxerr->shm_plcp1);
		bcm_bprintf(b, " shm_plcp2: 0x%x\n", phytxerr->shm_plcp2);
		bcm_bprintf(b, " shm_vht_sigb0: 0x%x\n", phytxerr->shm_vht_sigb0);
		bcm_bprintf(b, " shm_vht_sigb1: 0x%x\n", phytxerr->shm_vht_sigb1);
		bcm_bprintf(b, " shm_tx_tst: 0x%x\n", phytxerr->shm_tx_tst);
		bcm_bprintf(b, " shm_txerr_tm: 0x%x\n", phytxerr->shm_txerr_tm);
		bcm_bprintf(b, " shm_curchannel: 0x%x\n", phytxerr->shm_curchannel);
		bcm_bprintf(b, " shm_blk_crx_rxtsf_pos: 0x%x\n", phytxerr->shm_crx_rxtsf_pos);
		bcm_bprintf(b, " shm_lasttx_tsf: 0x%x\n", phytxerr->shm_lasttx_tsf);
		bcm_bprintf(b, " shm_s_rxtsftmrval: 0x%x\n", phytxerr->shm_s_rxtsftmrval);
		bcm_bprintf(b, " Phy_0x29: 0x%x\n", phytxerr->i16_0x29);
		bcm_bprintf(b, " Phy_0x2a: 0x%x\n", phytxerr->i16_0x2a);
	}
	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_MAC_SUSP);
	if (tlv) {
		const hnd_ext_trap_macsusp_t* macsusp;
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_MAC_SUSP), tlv->len);
		macsusp = (const hnd_ext_trap_macsusp_t *)tlv->data;
		bcm_bprintf(b, " version: %d\n", macsusp->version);
		bcm_bprintf(b, " trap_reason: %d\n", macsusp->trap_reason);
		bcm_bprintf(b, " maccontrol: 0x%x\n", macsusp->i32_maccontrol);
		bcm_bprintf(b, " maccommand: 0x%x\n", macsusp->i32_maccommand);
		bcm_bprintf(b, " macintstatus: 0x%x\n", macsusp->i32_macintstatus);
		for (i = 0; i < 4; i++)
			bcm_bprintf(b, " phydebug[%d]: 0x%x\n", i, macsusp->i32_phydebug[i]);
		for (i = 0; i < 8; i++)
			bcm_bprintf(b, " psmdebug[%d]: 0x%x\n", i, macsusp->i32_psmdebug[i]);
		bcm_bprintf(b, " Rxe Status_1: 0x%x\n", macsusp->i16_0x41a);
		bcm_bprintf(b, " Rxe Status_2: 0x%x\n", macsusp->i16_0x41c);
		bcm_bprintf(b, " PSM BRC: 0x%x\n", macsusp->i16_0x490);
		bcm_bprintf(b, " TXE Status: 0x%x\n", macsusp->i16_0x50e);
		bcm_bprintf(b, " TXE xmtdmabusy: 0x%x\n", macsusp->i16_0x55e);
		bcm_bprintf(b, " TXE XMTfifosuspflush: 0x%x\n", macsusp->i16_0x566);
		bcm_bprintf(b, " IFS Stat: 0x%x\n", macsusp->i16_0x690);
		bcm_bprintf(b, " IFS MEDBUSY CTR: 0x%x\n", macsusp->i16_0x692);
		bcm_bprintf(b, " IFS TX DUR: 0x%x\n", macsusp->i16_0x694);
		bcm_bprintf(b, " WEP CTL: 0x%x\n", macsusp->i16_0x7c0);
		bcm_bprintf(b, " TXE AQM fifo Ready: 0x%x\n", macsusp->i16_0x838);
		bcm_bprintf(b, " MHP status: 0x%x\n", macsusp->i16_0x880);
		bcm_bprintf(b, " shm_prewds_cnt: 0x%x\n", macsusp->shm_prewds_cnt);
		bcm_bprintf(b, " shm_ucode_dbgst: 0x%x\n", macsusp->shm_ucode_dbgst);
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_MAC_WAKE);
	if (tlv) {
		const hnd_ext_trap_macenab_t* macwake;
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_MAC_WAKE), tlv->len);
		macwake = (const hnd_ext_trap_macenab_t *)tlv->data;
		bcm_bprintf(b, " version: 0x%x\n", macwake->version);
		bcm_bprintf(b, " trap_reason: 0x%x\n", macwake->trap_reason);
		bcm_bprintf(b, " maccontrol: 0x%x\n", macwake->i32_maccontrol);
		bcm_bprintf(b, " maccommand: 0x%x\n", macwake->i32_maccommand);
		bcm_bprintf(b, " macintstatus: 0x%x\n", macwake->i32_macintstatus);
		for (i = 0; i < 8; i++)
			bcm_bprintf(b, " psmdebug[%d]: 0x%x\n", i, macwake->i32_psmdebug[i]);
		bcm_bprintf(b, " clk_ctl_st: 0x%x\n", macwake->i32_clk_ctl_st);
		bcm_bprintf(b, " powerctl: 0x%x\n", macwake->i32_powerctl);
		bcm_bprintf(b, " gated clock en: 0x%x\n", macwake->i16_0x1a8);
		bcm_bprintf(b, " PSM_SLP_TMR: 0x%x\n", macwake->i16_0x480);
		bcm_bprintf(b, " PSM BRC: 0x%x\n", macwake->i16_0x490);
		bcm_bprintf(b, " TSF CTL: 0x%x\n", macwake->i16_0x600);
		bcm_bprintf(b, " IFS Stat: 0x%x\n", macwake->i16_0x690);
		bcm_bprintf(b, " IFS_MEDBUSY_CTR: 0x%x\n", macwake->i16_0x692);
		bcm_bprintf(b, " Slow_CTL: 0x%x\n", macwake->i16_0x6a0);
		bcm_bprintf(b, " Slow_FRAC: 0x%x\n", macwake->i16_0x6a6);
		bcm_bprintf(b, " fast power up delay: 0x%x\n", macwake->i16_0x6a8);
		bcm_bprintf(b, " Slow_PER: 0x%x\n", macwake->i16_0x6aa);
		bcm_bprintf(b, " shm_ucode_dbgst: 0x%x\n", macwake->shm_ucode_dbgst);
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_BUS);
	if (tlv) {
		const bcm_dngl_pcie_hc_t* hc;
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_BUS), tlv->len);
		hc = (const bcm_dngl_pcie_hc_t *)tlv->data;
		bcm_bprintf(b, " version: 0x%x\n", hc->version);
		bcm_bprintf(b, " reserved: 0x%x\n", hc->reserved);
		bcm_bprintf(b, " pcie_err_ind_type: 0x%x\n", hc->pcie_err_ind_type);
		bcm_bprintf(b, " pcie_flag: 0x%x\n", hc->pcie_flag);
		bcm_bprintf(b, " pcie_control_reg: 0x%x\n", hc->pcie_control_reg);
		for (i = 0; i < HC_PCIEDEV_CONFIG_REGLIST_MAX; i++)
			bcm_bprintf(b, " pcie_config_regs[%d]: 0x%x\n", i, hc->pcie_config_regs[i]);
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_HMAP);
	if (tlv) {
		const pcie_hmapviolation_t* hmap;
		hmap = (const pcie_hmapviolation_t *)tlv->data;
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_HMAP), tlv->len);
		bcm_bprintf(b, " HMAP Vio Addr Low: 0x%x\n", hmap->hmap_violationaddr_lo);
		bcm_bprintf(b, " HMAP Vio Addr Hi: 0x%x\n", hmap->hmap_violationaddr_hi);
		bcm_bprintf(b, " HMAP Vio Info: 0x%x\n", hmap->hmap_violation_info);
	}

	tlv = bcm_parse_tlvs(hdr->data, hdr->len, TAG_TRAP_MEM_BIT_FLIP);
	if (tlv) {
		const hnd_ext_trap_fb_mem_err_t* fbit;
		bcm_bprintf(b, "\n%s len: %d\n", etd_trap_name(TAG_TRAP_MEM_BIT_FLIP), tlv->len);
		fbit = (const hnd_ext_trap_fb_mem_err_t *)tlv->data;
		bcm_bprintf(b, " version: %d\n", fbit->version);
		bcm_bprintf(b, " flip_bit_err_time: %d\n", fbit->flip_bit_err_time);
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
	msgbuf_ring_t *ctrl_ring = &prot->h2dring_ctrl_subn;

	if ((tlvs == NULL) || (tlv_len == 0)) {
		DHD_ERROR(("%s: argument error tlv: %p, tlv_len %d\n",
			__FUNCTION__, tlvs, tlv_len));
		return -1;
	}

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhdp->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_RING_LOCK(ctrl_ring->ring_lock, flags);

	/* if Host TS req already pending go away */
	if (prot->hostts_req_buf_inuse == TRUE) {
		DHD_ERROR(("one host TS request already pending at device\n"));
		DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhdp->bus);
#endif
		return -1;
	}

	/* Request for cbuf space */
	ts_req = (host_timestamp_msg_t*)dhd_prot_alloc_ring_space(dhdp, ctrl_ring,
		DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D,	&alloced, FALSE);
	if (ts_req == NULL) {
		DHD_ERROR(("couldn't allocate space on msgring to send host TS request\n"));
		DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhdp->bus);
#endif
		return -1;
	}

	/* Common msg buf hdr */
	ts_req->msg.msg_type = MSG_TYPE_HOSTTIMSTAMP;
	ts_req->msg.if_id = 0;
	ts_req->msg.flags =  ctrl_ring->current_phase;
	ts_req->msg.request_id = DHD_H2D_HOSTTS_REQ_PKTID;

	ts_req->msg.epoch = ctrl_ring->seqnum % H2D_EPOCH_MODULO;
	ctrl_ring->seqnum++;

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
	dhd_prot_ring_write_complete(dhdp, ctrl_ring, ts_req,
		DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D);

	DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

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

bool
dhd_prot_pkt_noretry(dhd_pub_t *dhd, bool enable, bool set)
{
	if (set)
		dhd->prot->no_retry = enable;

	return dhd->prot->no_retry;
}

bool
dhd_prot_pkt_noaggr(dhd_pub_t *dhd, bool enable, bool set)
{
	if (set)
		dhd->prot->no_aggr = enable;

	return dhd->prot->no_aggr;
}

bool
dhd_prot_pkt_fixed_rate(dhd_pub_t *dhd, bool enable, bool set)
{
	if (set)
		dhd->prot->fixed_rate = enable;

	return dhd->prot->fixed_rate;
}
#endif /* BCMPCIE */

void
dhd_prot_dma_indx_free(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;

	dhd_dma_buf_free(dhd, &prot->h2d_dma_indx_wr_buf);
	dhd_dma_buf_free(dhd, &prot->d2h_dma_indx_rd_buf);
}

void
dhd_msgbuf_delay_post_ts_bufs(dhd_pub_t *dhd)
{
	if (dhd->prot->max_tsbufpost > 0)
		dhd_msgbuf_rxbuf_post_ts_bufs(dhd);
}

static void
BCMFASTPATH(dhd_prot_process_fw_timestamp)(dhd_pub_t *dhd, void* buf)
{
#ifdef DHD_TIMESYNC
	fw_timestamp_event_msg_t *resp;
	uint32 pktid;
	uint16 buflen, seqnum;
	void * pkt;

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

	if (!dhd_timesync_delay_post_bufs(dhd)) {
		if (dhd->prot->max_tsbufpost > 0) {
			dhd_msgbuf_rxbuf_post_ts_bufs(dhd);
		}
	}

	pkt = dhd_prot_packet_get(dhd, pktid, PKTTYPE_TSBUF_RX, TRUE);

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

uint16
dhd_prot_get_ioctl_trans_id(dhd_pub_t *dhdp)
{
	return dhdp->prot->ioctl_trans_id;
}

#ifdef SNAPSHOT_UPLOAD
/* send request to take snapshot */
int
dhd_prot_send_snapshot_request(dhd_pub_t *dhdp, uint8 snapshot_type, uint8 snapshot_param)
{
	dhd_prot_t *prot = dhdp->prot;
	dhd_dma_buf_t *dma_buf = &prot->snapshot_upload_buf;
	snapshot_upload_request_msg_t *snap_req;
	unsigned long flags;
	uint16 alloced = 0;
	msgbuf_ring_t *ctrl_ring = &prot->h2dring_ctrl_subn;

#ifdef PCIE_INB_DW
	if (dhd_prot_inc_hostactive_devwake_assert(dhdp->bus) != BCME_OK)
		return BCME_ERROR;
#endif /* PCIE_INB_DW */

	DHD_RING_LOCK(ctrl_ring->ring_lock, flags);

	/* Request for cbuf space */
	snap_req = (snapshot_upload_request_msg_t *)dhd_prot_alloc_ring_space(dhdp,
		ctrl_ring, DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D,
		&alloced, FALSE);
	if (snap_req == NULL) {
		DHD_ERROR(("couldn't allocate space on msgring to send snapshot request\n"));
		DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);
#ifdef PCIE_INB_DW
		dhd_prot_dec_hostactive_ack_pending_dsreq(dhdp->bus);
#endif
		return BCME_ERROR;
	}

	/* Common msg buf hdr */
	snap_req->cmn_hdr.msg_type = MSG_TYPE_SNAPSHOT_UPLOAD;
	snap_req->cmn_hdr.if_id = 0;
	snap_req->cmn_hdr.flags =  ctrl_ring->current_phase;
	snap_req->cmn_hdr.request_id = DHD_H2D_SNAPSHOT_UPLOAD_REQ_PKTID;
	snap_req->cmn_hdr.epoch = ctrl_ring->seqnum % H2D_EPOCH_MODULO;
	ctrl_ring->seqnum++;

	/* snapshot request msg */
	snap_req->snapshot_buf_len = htol32(dma_buf->len);
	snap_req->snapshot_type = snapshot_type;
	snap_req->snapshot_param = snapshot_param;
	snap_req->host_buf_addr.high = htol32(PHYSADDRHI(dma_buf->pa));
	snap_req->host_buf_addr.low = htol32(PHYSADDRLO(dma_buf->pa));

	if (ISALIGNED(dma_buf->va, DMA_ALIGN_LEN) == FALSE) {
		DHD_ERROR(("snapshot req buffer address unaligned !!!!! \n"));
	}

	/* clear previous snapshot upload */
	memset(dma_buf->va, 0, dma_buf->len);
	prot->snapshot_upload_len = 0;
	prot->snapshot_type = snapshot_type;
	prot->snapshot_cmpl_pending = TRUE;

	DHD_CTL(("submitted snapshot request request_id %d, buf_len %d, type %d, param %d\n",
		snap_req->cmn_hdr.request_id, snap_req->snapshot_buf_len,
		snap_req->snapshot_type, snap_req->snapshot_param));

	/* upd wrt ptr and raise interrupt */
	dhd_prot_ring_write_complete(dhdp, ctrl_ring, snap_req,
		DHD_FLOWRING_DEFAULT_NITEMS_POSTED_H2D);

	DHD_RING_UNLOCK(ctrl_ring->ring_lock, flags);

#ifdef PCIE_INB_DW
	dhd_prot_dec_hostactive_ack_pending_dsreq(dhdp->bus);
#endif

	return BCME_OK;
} /* dhd_prot_send_snapshot_request */

/* get uploaded snapshot */
int
dhd_prot_get_snapshot(dhd_pub_t *dhdp, uint8 snapshot_type, uint32 offset,
	uint32 dst_buf_size, uint8 *dst_buf, uint32 *dst_size, bool *is_more)
{
	dhd_prot_t *prot = dhdp->prot;
	uint8 *buf = prot->snapshot_upload_buf.va;
	uint8 *buf_end = buf + prot->snapshot_upload_len;
	uint32 copy_size;

	/* snapshot type must match */
	if (prot->snapshot_type != snapshot_type) {
		return BCME_DATA_NOTFOUND;
	}

	/* snapshot not completed */
	if (prot->snapshot_cmpl_pending) {
		return BCME_NOTREADY;
	}

	/* offset within the buffer */
	if (buf + offset >= buf_end) {
		return BCME_BADARG;
	}

	/* copy dst buf size or remaining size */
	copy_size = MIN(dst_buf_size, buf_end - (buf + offset));
	memcpy(dst_buf, buf + offset, copy_size);

	/* return size and is_more */
	*dst_size = copy_size;
	*is_more = (offset + copy_size < prot->snapshot_upload_len) ?
		TRUE : FALSE;
	return BCME_OK;
} /* dhd_prot_get_snapshot */

#endif	/* SNAPSHOT_UPLOAD */

int dhd_get_hscb_info(dhd_pub_t *dhd, void ** va, uint32 *len)
{
	if (!dhd->hscb_enable) {
		if (len) {
			/* prevent "Operation not supported" dhd message */
			*len = 0;
			return BCME_OK;
		}
		return BCME_UNSUPPORTED;
	}

	if (va) {
		*va = dhd->prot->host_scb_buf.va;
	}
	if (len) {
		*len = dhd->prot->host_scb_buf.len;
	}

	return BCME_OK;
}

#ifdef DHD_BUS_MEM_ACCESS
int dhd_get_hscb_buff(dhd_pub_t *dhd, uint32 offset, uint32 length, void * buff)
{
	if (!dhd->hscb_enable) {
		return BCME_UNSUPPORTED;
	}

	if (dhd->prot->host_scb_buf.va == NULL ||
		((uint64)offset + length > (uint64)dhd->prot->host_scb_buf.len)) {
		return BCME_BADADDR;
	}

	memcpy(buff, (char*)dhd->prot->host_scb_buf.va + offset, length);

	return BCME_OK;
}
#endif /* DHD_BUS_MEM_ACCESS */

#ifdef DHD_HP2P
uint32
dhd_prot_pkt_threshold(dhd_pub_t *dhd, bool set, uint32 val)
{
	if (set)
		dhd->pkt_thresh = (uint16)val;

	val = dhd->pkt_thresh;

	return val;
}

uint32
dhd_prot_time_threshold(dhd_pub_t *dhd, bool set, uint32 val)
{
	if (set)
		dhd->time_thresh = (uint16)val;

	val = dhd->time_thresh;

	return val;
}

uint32
dhd_prot_pkt_expiry(dhd_pub_t *dhd, bool set, uint32 val)
{
	if (set)
		dhd->pkt_expiry = (uint16)val;

	val = dhd->pkt_expiry;

	return val;
}

uint8
dhd_prot_hp2p_enable(dhd_pub_t *dhd, bool set, int enable)
{
	uint8 ret = 0;
	if (set) {
		dhd->hp2p_enable = (enable & 0xf) ? TRUE : FALSE;
		dhd->hp2p_infra_enable = ((enable >> 4) & 0xf) ? TRUE : FALSE;

		if (enable) {
			dhd_update_flow_prio_map(dhd, DHD_FLOW_PRIO_TID_MAP);
		} else {
			dhd_update_flow_prio_map(dhd, DHD_FLOW_PRIO_AC_MAP);
		}
	}
	ret = dhd->hp2p_infra_enable ? 0x1:0x0;
	ret <<= 4;
	ret |= dhd->hp2p_enable ? 0x1:0x0;

	return ret;
}

static void
dhd_update_hp2p_rxstats(dhd_pub_t *dhd, host_rxbuf_cmpl_t *rxstatus)
{
	ts_timestamp_t *ts = (ts_timestamp_t *)&rxstatus->ts;
	hp2p_info_t *hp2p_info;
	uint32 dur1;

	hp2p_info = &dhd->hp2p_info[0];
	dur1 = ((ts->high & 0x3FF) * HP2P_TIME_SCALE) / 100;

	if (dur1 > (MAX_RX_HIST_BIN - 1)) {
		dur1 = MAX_RX_HIST_BIN - 1;
		DHD_INFO(("%s: 0x%x 0x%x\n",
			__FUNCTION__, ts->low, ts->high));
	}

	hp2p_info->rx_t0[dur1 % MAX_RX_HIST_BIN]++;
	return;
}

static void
dhd_update_hp2p_txstats(dhd_pub_t *dhd, host_txbuf_cmpl_t *txstatus)
{
	ts_timestamp_t *ts = (ts_timestamp_t *)&txstatus->ts;
	uint16 flowid = txstatus->compl_hdr.flow_ring_id;
	uint32 hp2p_flowid, dur1, dur2;
	hp2p_info_t *hp2p_info;

	hp2p_flowid = dhd->bus->max_submission_rings -
		dhd->bus->max_cmn_rings - flowid + 1;
	hp2p_info = &dhd->hp2p_info[hp2p_flowid];
	ts = (ts_timestamp_t *)&(txstatus->ts);

	dur1 = ((ts->high & 0x3FF) * HP2P_TIME_SCALE) / 1000;
	if (dur1 > (MAX_TX_HIST_BIN - 1)) {
		dur1 = MAX_TX_HIST_BIN - 1;
		DHD_INFO(("%s: 0x%x 0x%x\n", __FUNCTION__, ts->low, ts->high));
	}
	hp2p_info->tx_t0[dur1 % MAX_TX_HIST_BIN]++;

	dur2 = (((ts->high >> 10) & 0x3FF) * HP2P_TIME_SCALE) / 1000;
	if (dur2 > (MAX_TX_HIST_BIN - 1)) {
		dur2 = MAX_TX_HIST_BIN - 1;
		DHD_INFO(("%s: 0x%x 0x%x\n", __FUNCTION__, ts->low, ts->high));
	}

	hp2p_info->tx_t1[dur2 % MAX_TX_HIST_BIN]++;
	return;
}

enum hrtimer_restart dhd_hp2p_write(struct hrtimer *timer)
{
	hp2p_info_t *hp2p_info;
	unsigned long flags;
	dhd_pub_t *dhdp;

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	hp2p_info = container_of(timer, hp2p_info_t, timer);
	GCC_DIAGNOSTIC_POP();

	dhdp = hp2p_info->dhd_pub;
	if (!dhdp) {
		goto done;
	}

	DHD_INFO(("%s: pend_item = %d flowid = %d\n",
		__FUNCTION__, ((msgbuf_ring_t *)hp2p_info->ring)->pend_items_count,
		hp2p_info->flowid));

	flags = dhd_os_hp2plock(dhdp);

	dhd_prot_txdata_write_flush(dhdp, hp2p_info->flowid);
	hp2p_info->hrtimer_init = FALSE;
	hp2p_info->num_timer_limit++;

	dhd_os_hp2punlock(dhdp, flags);
done:
	return HRTIMER_NORESTART;
}

static void
dhd_calc_hp2p_burst(dhd_pub_t *dhd, msgbuf_ring_t *ring, uint16 flowid)
{
	hp2p_info_t *hp2p_info;
	uint16 hp2p_flowid;

	hp2p_flowid = dhd->bus->max_submission_rings -
		dhd->bus->max_cmn_rings - flowid + 1;
	hp2p_info = &dhd->hp2p_info[hp2p_flowid];

	if (ring->pend_items_count == dhd->pkt_thresh) {
		dhd_prot_txdata_write_flush(dhd, flowid);

		hp2p_info->hrtimer_init = FALSE;
		hp2p_info->ring = NULL;
		hp2p_info->num_pkt_limit++;
		hrtimer_cancel(&hp2p_info->timer);

		DHD_INFO(("%s: cancel hrtimer for flowid = %d \n"
			"hp2p_flowid = %d pkt_thresh = %d\n",
			__FUNCTION__, flowid, hp2p_flowid, dhd->pkt_thresh));
	} else {
		if (hp2p_info->hrtimer_init == FALSE) {
			hp2p_info->hrtimer_init = TRUE;
			hp2p_info->flowid = flowid;
			hp2p_info->dhd_pub = dhd;
			hp2p_info->ring = ring;
			hp2p_info->num_timer_start++;

			hrtimer_start(&hp2p_info->timer,
				ktime_set(0, dhd->time_thresh * 1000), HRTIMER_MODE_REL);

			DHD_INFO(("%s: start hrtimer for flowid = %d hp2_flowid = %d\n",
					__FUNCTION__, flowid, hp2p_flowid));
		}
	}
	return;
}

static void
dhd_update_hp2p_txdesc(dhd_pub_t *dhd, host_txbuf_post_t *txdesc)
{
	uint64 ts;

	ts = local_clock();
	do_div(ts, 1000);

	txdesc->metadata_buf_len = 0;
	txdesc->metadata_buf_addr.high_addr = htol32((ts >> 32) & 0xFFFFFFFF);
	txdesc->metadata_buf_addr.low_addr = htol32(ts & 0xFFFFFFFF);
	txdesc->exp_time = dhd->pkt_expiry;

	DHD_INFO(("%s: metadata_high = 0x%x metadata_low = 0x%x exp_time = %x\n",
		__FUNCTION__, txdesc->metadata_buf_addr.high_addr,
		txdesc->metadata_buf_addr.low_addr,
		txdesc->exp_time));

	return;
}
#endif /* DHD_HP2P */

#ifdef DHD_MAP_LOGGING
void
dhd_prot_smmu_fault_dump(dhd_pub_t *dhdp)
{
	dhd_prot_debug_info_print(dhdp);
	OSL_DMA_MAP_DUMP(dhdp->osh);
#ifdef DHD_MAP_PKTID_LOGGING
	dhd_pktid_logging_dump(dhdp);
#endif /* DHD_MAP_PKTID_LOGGING */
#ifdef DHD_FW_COREDUMP
	dhdp->memdump_type = DUMP_TYPE_SMMU_FAULT;
#ifdef DNGL_AXI_ERROR_LOGGING
	dhdp->memdump_enabled = DUMP_MEMFILE;
	dhd_bus_get_mem_dump(dhdp);
#else
	dhdp->memdump_enabled = DUMP_MEMONLY;
	dhd_bus_mem_dump(dhdp);
#endif /* DNGL_AXI_ERROR_LOGGING */
#endif /* DHD_FW_COREDUMP */
}
#endif /* DHD_MAP_LOGGING */

#ifdef DHD_FLOW_RING_STATUS_TRACE
void
dhd_dump_bus_flow_ring_status_trace(
	dhd_bus_t *bus, struct bcmstrbuf *strbuf, dhd_frs_trace_t *frs_trace, int dumpsz, char *str)
{
	int i;
	dhd_prot_t *prot = bus->dhd->prot;
	uint32 isr_cnt = bus->frs_isr_count % FRS_TRACE_SIZE;
	uint32 dpc_cnt = bus->frs_dpc_count % FRS_TRACE_SIZE;

	bcm_bprintf(strbuf, "---- %s ------ isr_cnt: %d dpc_cnt %d\n",
		str, isr_cnt, dpc_cnt);
	bcm_bprintf(strbuf, "%s\t%s\t%s\t%s\t%s\t%s\t",
		"Timestamp ns", "H2DCtrlPost", "D2HCtrlCpl",
		"H2DRxPost", "D2HRxCpl", "D2HTxCpl");
	if (prot->h2dring_info_subn != NULL && prot->d2hring_info_cpln != NULL) {
		bcm_bprintf(strbuf, "%s\t%s\t", "H2DRingInfoPost", "D2HRingInfoCpl");
	}
	if (prot->d2hring_edl != NULL) {
		bcm_bprintf(strbuf, "%s", "D2HRingEDL");
	}
	bcm_bprintf(strbuf, "\n");
	for (i = 0; i < dumpsz; i ++) {
		bcm_bprintf(strbuf, "%llu\t%6u-%u\t%6u-%u\t%6u-%u\t%6u-%u\t%6u-%u\t",
				frs_trace[i].timestamp,
				frs_trace[i].h2d_ctrl_post_drd,
				frs_trace[i].h2d_ctrl_post_dwr,
				frs_trace[i].d2h_ctrl_cpln_drd,
				frs_trace[i].d2h_ctrl_cpln_dwr,
				frs_trace[i].h2d_rx_post_drd,
				frs_trace[i].h2d_rx_post_dwr,
				frs_trace[i].d2h_rx_cpln_drd,
				frs_trace[i].d2h_rx_cpln_dwr,
				frs_trace[i].d2h_tx_cpln_drd,
				frs_trace[i].d2h_tx_cpln_dwr);
		if (prot->h2dring_info_subn != NULL && prot->d2hring_info_cpln != NULL) {
			bcm_bprintf(strbuf, "%6u-%u\t%6u-%u\t",
				frs_trace[i].h2d_info_post_drd,
				frs_trace[i].h2d_info_post_dwr,
				frs_trace[i].d2h_info_cpln_drd,
				frs_trace[i].d2h_info_cpln_dwr);
		}
		if (prot->d2hring_edl != NULL) {
			bcm_bprintf(strbuf, "%6u-%u",
				frs_trace[i].d2h_ring_edl_drd,
				frs_trace[i].d2h_ring_edl_dwr);

		}
		bcm_bprintf(strbuf, "\n");
	}
	bcm_bprintf(strbuf, "--------------------------\n");
}

void
dhd_dump_bus_flow_ring_status_isr_trace(dhd_bus_t *bus, struct bcmstrbuf *strbuf)
{
	int dumpsz;

	dumpsz = bus->frs_isr_count < FRS_TRACE_SIZE ?
		bus->frs_isr_count : FRS_TRACE_SIZE;
	if (dumpsz == 0) {
		bcm_bprintf(strbuf, "\nEMPTY ISR FLOW RING TRACE\n");
		return;
	}
	dhd_dump_bus_flow_ring_status_trace(bus, strbuf, bus->frs_isr_trace,
		dumpsz, "ISR FLOW RING TRACE DRD-DWR");
}

void
dhd_dump_bus_flow_ring_status_dpc_trace(dhd_bus_t *bus, struct bcmstrbuf *strbuf)
{
	int dumpsz;

	dumpsz = bus->frs_dpc_count < FRS_TRACE_SIZE ?
		bus->frs_dpc_count : FRS_TRACE_SIZE;
	if (dumpsz == 0) {
		bcm_bprintf(strbuf, "\nEMPTY ISR FLOW RING TRACE\n");
		return;
	}
	dhd_dump_bus_flow_ring_status_trace(bus, strbuf, bus->frs_dpc_trace,
		dumpsz, "DPC FLOW RING TRACE DRD-DWR");
}
static void
dhd_bus_flow_ring_status_trace(dhd_pub_t *dhd, dhd_frs_trace_t *frs_trace)
{
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring;

	ring = &prot->h2dring_ctrl_subn;
	frs_trace->h2d_ctrl_post_drd =
		dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, ring->idx);
	frs_trace->h2d_ctrl_post_dwr =
		dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_WR_UPD, ring->idx);

	ring = &prot->d2hring_ctrl_cpln;
	frs_trace->d2h_ctrl_cpln_drd =
		dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
	frs_trace->d2h_ctrl_cpln_dwr =
		dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);

	ring = &prot->h2dring_rxp_subn;
	frs_trace->h2d_rx_post_drd =
		dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, ring->idx);
	frs_trace->h2d_rx_post_dwr =
		dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_WR_UPD, ring->idx);

	ring = &prot->d2hring_rx_cpln;
	frs_trace->d2h_rx_cpln_drd =
		dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
	frs_trace->d2h_rx_cpln_dwr =
		dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);

	ring = &prot->d2hring_tx_cpln;
	frs_trace->d2h_tx_cpln_drd =
		dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
	frs_trace->d2h_tx_cpln_dwr =
		dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);

	if (dhd->prot->h2dring_info_subn != NULL && dhd->prot->d2hring_info_cpln != NULL) {
		ring = prot->h2dring_info_subn;
		frs_trace->h2d_info_post_drd =
			dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, ring->idx);
		frs_trace->h2d_info_post_dwr =
			dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_WR_UPD, ring->idx);

		ring = prot->d2hring_info_cpln;
		frs_trace->d2h_info_cpln_drd =
			dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
		frs_trace->d2h_info_cpln_dwr =
			dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
	}
	if (prot->d2hring_edl != NULL) {
		ring = prot->d2hring_edl;
		frs_trace->d2h_ring_edl_drd =
			dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
		frs_trace->d2h_ring_edl_dwr =
			dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
	}

}

void
dhd_bus_flow_ring_status_isr_trace(dhd_pub_t *dhd)
{
	uint32 cnt = dhd->bus->frs_isr_count % FRS_TRACE_SIZE;
	dhd_frs_trace_t *frs_isr_trace = &dhd->bus->frs_isr_trace[cnt];
	uint64 time_ns_prev = frs_isr_trace[cnt].timestamp;
	uint64 time_ns_now = OSL_LOCALTIME_NS();

	if ((time_ns_now - time_ns_prev) < 250000) { /* delta less than 250us */
		return;
	}

	dhd_bus_flow_ring_status_trace(dhd, frs_isr_trace);

	frs_isr_trace->timestamp = OSL_LOCALTIME_NS();
	dhd->bus->frs_isr_count ++;
}

void
dhd_bus_flow_ring_status_dpc_trace(dhd_pub_t *dhd)
{
	uint32 cnt = dhd->bus->frs_dpc_count % FRS_TRACE_SIZE;
	dhd_frs_trace_t *frs_dpc_trace = &dhd->bus->frs_dpc_trace[cnt];
	uint64 time_ns_prev = frs_dpc_trace[cnt].timestamp;
	uint64 time_ns_now = OSL_LOCALTIME_NS();

	if ((time_ns_now - time_ns_prev) < 250000) { /* delta less than 250us */
		return;
	}

	dhd_bus_flow_ring_status_trace(dhd, frs_dpc_trace);

	frs_dpc_trace->timestamp = OSL_LOCALTIME_NS();
	dhd->bus->frs_dpc_count ++;
}
#endif /* DHD_FLOW_RING_STATUS_TRACE */
