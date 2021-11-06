/*
 * Generic Broadcom Home Networking Division (HND) DMA engine SW interface
 * This supports the following chips: BCM42xx, 44xx, 47xx .
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
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef	_hnddma_h_
#define	_hnddma_h_

#include <typedefs.h>
#include <osl_decl.h>
#include <siutils.h>
#include <sbhnddma.h>
#include <hnd_pktq.h>
#include <hnd_pktpool.h>

#ifndef _hnddma_pub_
#define _hnddma_pub_
typedef const struct hnddma_pub hnddma_t;
#endif /* _hnddma_pub_ */

/* range param for dma_getnexttxp() and dma_txreclaim */
typedef enum txd_range {
	HNDDMA_RANGE_ALL		= 1,
	HNDDMA_RANGE_TRANSMITTED,
	HNDDMA_RANGE_TRANSFERED
} txd_range_t;

/* dma parameters id */
enum dma_param_id {
	HNDDMA_PID_TX_MULTI_OUTSTD_RD	= 0,
	HNDDMA_PID_TX_PREFETCH_CTL,
	HNDDMA_PID_TX_PREFETCH_THRESH,
	HNDDMA_PID_TX_BURSTLEN,
	HNDDMA_PID_TX_CHAN_SWITCH,

	HNDDMA_PID_RX_PREFETCH_CTL	= 0x100,
	HNDDMA_PID_RX_PREFETCH_THRESH,
	HNDDMA_PID_RX_BURSTLEN,
	HNDDMA_PID_BURSTLEN_CAP,
	HNDDMA_PID_BURSTLEN_WAR,
	HNDDMA_SEP_RX_HDR,	/**< SPLITRX related */
	HNDDMA_SPLIT_FIFO,
	HNDDMA_PID_D11RX_WAR,
	HNDDMA_PID_RX_WAIT_CMPL,
	HNDDMA_NRXPOST,
	HNDDMA_NRXBUFSZ,
	HNDDMA_PID_RXCTL_MOW,
	HNDDMA_M2M_RXBUF_RAW /* rx buffers are raw buffers, not lbufs/lfrags */
};

#define SPLIT_FIFO_0	1
#define SPLIT_FIFO_1	2

typedef void (*setup_context_t)(void *ctx, void *p, uint8 **desc0, uint16 *len0,
	uint8 **desc1, uint16 *len1);

/**
 * Exported data structure (read-only)
 */
/* export structure */
struct hnddma_pub {
	uint		dmastflags;	/* dma status flags */
	uint		dmactrlflags;	/**< dma control flags */

	/* rx error counters */
	uint		rxgiants;	/**< rx giant frames */
	uint		rxnobuf;	/**< rx out of dma descriptors */
	/* tx error counters */
	uint		txnobuf;	/**< tx out of dma descriptors */
	uint		txnodesc;	/**< tx out of dma descriptors running count */
};

/* DMA status flags */
#define BCM_DMA_STF_RX	(1u << 0u)	/* the channel is RX DMA */

typedef struct dma_common dma_common_t;
typedef struct dma_dd_pool dma_dd_pool_t;

/* Flags for dma_attach_ext function */
#define BCM_DMA_IND_INTF_FLAG		0x00000001	/* set for using INDIRECT DMA INTERFACE */
#define BCM_DMA_DESC_ONLY_FLAG		0x00000002	/* For DMA that posts descriptors only and
							 * no packets
							 */
#define BCM_DMA_CHAN_SWITCH_EN		0x00000008	/* for d11 corerev 64+ to help arbitrate
							 * btw dma channels.
							 */
#define BCM_DMA_ROEXT_SUPPORT		0x00000010	/* for d11 corerev 128+ to support receive
							 * frame offset >=128B and <= 255B
							 */
#define BCM_DMA_RX_ALIGN_8BYTE		0x00000020	/* RXDMA address 8-byte aligned */
#define BCM_DMA_DESC_SHARED_POOL	0x00000100	/* For TX DMA that uses shared desc pool */
#define BCM_DMA_RXP_LIST		0x00000200      /* linked list for RXP instead of array */

typedef int (*rxpkt_error_check_t)(const void* ctx, void* pkt);

extern dma_common_t * dma_common_attach(osl_t *osh, volatile uint32 *indqsel,
	volatile uint32 *suspreq, volatile uint32 *flushreq, rxpkt_error_check_t cb, void *ctx);
extern void dma_common_detach(dma_common_t *dmacommon);
extern void dma_common_set_ddpool_ctx(dma_common_t *dmacommon, void *desc_pool);
extern void * dma_common_get_ddpool_ctx(dma_common_t *dmacommon, void **va);
extern bool dma_check_last_desc(hnddma_t *dmah);
extern void dma_txfrwd(hnddma_t *dmah);

#ifdef BCM_DMA_INDIRECT
/* Use indirect registers for non-ctmode */
#define DMA_INDQSEL_IA	(1 << 31)
extern void dma_set_indqsel(hnddma_t *di, bool force);
extern bool dma_is_indirect(hnddma_t *dmah);
#else
#define dma_set_indqsel(a, b)
#define dma_is_indirect(a)	FALSE
#endif /* #ifdef BCM_DMA_INDIRECT */

extern hnddma_t * dma_attach_ext(dma_common_t *dmac, osl_t *osh, const char *name, si_t *sih,
	volatile void *dmaregstx, volatile void *dmaregsrx, uint32 flags, uint8 qnum,
	uint ntxd, uint nrxd, uint rxbufsize, int rxextheadroom, uint nrxpost, uint rxoffset,
	uint *msg_level, uint coreunit);

extern hnddma_t * dma_attach(osl_t *osh, const char *name, si_t *sih,
	volatile void *dmaregstx, volatile void *dmaregsrx,
	uint ntxd, uint nrxd, uint rxbufsize, int rxextheadroom, uint nrxpost,
	uint rxoffset, uint *msg_level);

void dma_rx_desc_init(hnddma_t *dmah, uint rxfifo);
void dma_detach(hnddma_t *dmah);
bool dma_txreset(hnddma_t *dmah);
bool dma_rxreset(hnddma_t *dmah);
bool dma_rxidle(hnddma_t *dmah);
void dma_txinit(hnddma_t *dmah);
bool dma_txenabled(hnddma_t *dmah);
void dma_rxinit(hnddma_t *dmah);
void dma_txsuspend(hnddma_t *dmah);
void dma_txresume(hnddma_t *dmah);
bool dma_txsuspended(hnddma_t *dmah);
bool dma_txsuspendedidle(hnddma_t *dmah);
void dma_txflush(hnddma_t *dmah);
void dma_txflush_clear(hnddma_t *dmah);
int dma_txfast_ext(hnddma_t *dmah, void *p0, bool commit, uint16 *pre_txout, uint16 *numd);
int dma_txfast_alfrag(hnddma_t *dmah, hnddma_t *aqm_dmah, void *p, bool commit, dma64dd_t *aqmdesc,
	uint d11_txh_len, bool ptxd_hw_enab);
#define dma_txfast(dmah, p0, commit) \
		dma_txfast_ext((dmah), (p0), (commit), NULL, NULL)
void dma_txcommit(hnddma_t *dmah);
int dma_txunframed(hnddma_t *dmah, void *buf, uint len, bool commit);
void *dma_getpos(hnddma_t *dmah, bool direction);
void dma_fifoloopbackenable(hnddma_t *dmah);
void dma_fifoloopbackdisable(hnddma_t *dmah);
bool dma_txstopped(hnddma_t *dmah);
bool dma_rxstopped(hnddma_t *dmah);
void dma_rxenable(hnddma_t *dmah);
bool dma_rxenabled(hnddma_t *dmah);
void *dma_rx(hnddma_t *dmah);
#ifdef APP_RX
void dma_getnextrxp_app(hnddma_t *dmah, bool forceall, uint *pktcnt,
	void **head, void **tail);
void dma_rxfill_haddr_getparams(hnddma_t *dmah, uint *nrxd, uint16 *rxout,
	dma64dd_t **ddring, uint *rxextrahdrroom, uint32 **rxpktid);
void dma_rxfill_haddr_setparams(hnddma_t *dmah, uint16 rxout);
#endif /* APP_RX */
uint dma_rx_get_rxoffset(hnddma_t *dmah);
bool dma_rxfill(hnddma_t *dmah);
bool dma_rxfill_required(hnddma_t *dmah);
void dma_txreclaim(hnddma_t *dmah, txd_range_t range);
void dma_rxreclaim(hnddma_t *dmah);
#define _DMA_GETUINTVARPTR_
uint *dma_getuintvarptr(hnddma_t *dmah, const char *name);
uint8 dma_getuint8var(hnddma_t *dmah, const char *name);
uint16 dma_getuint16var(hnddma_t *dmah, const char *name);
uint32 dma_getuint32var(hnddma_t *dmah, const char *name);
void * dma_getnexttxp(hnddma_t *dmah, txd_range_t range);
void * dma_getnextp(hnddma_t *dmah);
void * dma_getnextrxp(hnddma_t *dmah, bool forceall);
void * dma_peeknexttxp(hnddma_t *dmah, txd_range_t range);
int dma_peekntxp(hnddma_t *dmah, int *len, void *txps[], txd_range_t range);
void * dma_peeknextrxp(hnddma_t *dmah);
void dma_rxparam_get(hnddma_t *dmah, uint16 *rxoffset, uint16 *rxbufsize);
bool dma_is_rxfill_suspend(hnddma_t *dmah);
void dma_txblock(hnddma_t *dmah);
void dma_txunblock(hnddma_t *dmah);
uint dma_txactive(hnddma_t *dmah);
uint dma_rxactive(hnddma_t *dmah);
void dma_txrotate(hnddma_t *dmah);
void dma_counterreset(hnddma_t *dmah);
uint dma_ctrlflags(hnddma_t *dmah, uint mask, uint flags);
uint dma_txpending(hnddma_t *dmah);
uint dma_txcommitted(hnddma_t *dmah);
int dma_pktpool_set(hnddma_t *dmah, pktpool_t *pool);
int dma_rxdatapool_set(hnddma_t *dmah, pktpool_t *pktpool);
pktpool_t *dma_rxdatapool_get(hnddma_t *dmah);

void dma_dump_txdmaregs(hnddma_t *dmah, uint32 **buf);
void dma_dump_rxdmaregs(hnddma_t *dmah, uint32 **buf);
#if defined(BCMDBG) || defined(BCMDBG_DUMP) || defined(BCMDBG_DMA)
void dma_dump(hnddma_t *dmah, struct bcmstrbuf *b, bool dumpring);
void dma_dumptx(hnddma_t *dmah, struct bcmstrbuf *b, bool dumpring);
void dma_dumprx(hnddma_t *dmah, struct bcmstrbuf *b, bool dumpring);
#endif
bool dma_rxtxerror(hnddma_t *dmah, bool istx);
void dma_burstlen_set(hnddma_t *dmah, uint8 rxburstlen, uint8 txburstlen);
uint dma_avoidance_cnt(hnddma_t *dmah);
void dma_param_set(hnddma_t *dmah, uint16 paramid, uint16 paramval);
void dma_param_get(hnddma_t *dmah, uint16 paramid, uint *paramval);
void dma_context(hnddma_t *dmah, setup_context_t fn, void *ctx);

bool dma_glom_enable(hnddma_t *dmah, uint32 val);
uint dma_activerxbuf(hnddma_t *dmah);
bool dma_rxidlestatus(hnddma_t *dmah);
uint dma_get_rxpost(hnddma_t *dmah);

/* return addresswidth allowed
 * This needs to be done after SB attach but before dma attach.
 * SB attach provides ability to probe backplane and dma core capabilities
 * This info is needed by DMA_ALLOC_CONSISTENT in dma attach
 */
extern uint dma_addrwidth(si_t *sih, void *dmaregs);

/* count the number of tx packets that are queued to the dma ring */
extern uint dma_txp(hnddma_t *di);

extern void dma_txrewind(hnddma_t *di);

/* pio helpers */
extern int dma_msgbuf_txfast(hnddma_t *di, dma64addr_t p0, bool com, uint32 ln, bool fst, bool lst);
extern int dma_ptrbuf_txfast(hnddma_t *dmah, dma64addr_t p0, void *p, bool commit,
	uint32 len, bool first, bool last);

extern int dma_rxfast(hnddma_t *di, dma64addr_t p, uint32 len);
extern int dma_rxfill_suspend(hnddma_t *dmah, bool suspended);
extern void dma_link_handle(hnddma_t *dmah1, hnddma_t *dmah2);
extern void dma_unlink_handle(hnddma_t *dmah1, hnddma_t *dmah2);
extern int dma_rxfill_unframed(hnddma_t *di, void *buf, uint len, bool commit);

extern uint16 dma_get_next_txd_idx(hnddma_t *di, bool txout);
extern uint16 dma_get_txd_count(hnddma_t *dmah, uint16 start, bool txout);
extern uintptr dma_get_txd_addr(hnddma_t *di, uint16 idx);

/* returns the memory address (hi and low) of the buffer associated with the dma descriptor
 * having index idx.
 */
extern void dma_get_txd_memaddr(hnddma_t *dmah, uint32 *addrlo, uint32 *addrhi, uint idx);

extern int dma_txdesc(hnddma_t *dmah, dma64dd_t *dd, bool commit);
extern int dma_nexttxdd(hnddma_t *dmah, txd_range_t range, uint32 *flags1, uint32 *flags2,
	bool advance);

extern void dma_update_rxfill(hnddma_t *dmah);
extern void dma_rxchan_reset(hnddma_t *di);
extern void dma_txchan_reset(hnddma_t *di);
extern void dma_chan_reset(hnddma_t *dmah);
extern pktpool_t* dma_pktpool_get(hnddma_t *dmah);
extern void dma_clearrxp(hnddma_t *dmah);
extern void dma_cleartxp(hnddma_t *dmah);

#define dma_getnexttxdd(dmah, range, flags1, flags2) \
		dma_nexttxdd((dmah), (range), (flags1), (flags2), TRUE)

#define dma_peeknexttxdd(dmah, range, flags1, flags2) \
		dma_nexttxdd((dmah), (range), (flags1), (flags2), FALSE)

#define NUM_VEC_PCIE	4

#define XFER_FROM_LBUF	0x1
#define XFER_TO_LBUF	0x2
#define XFER_INJ_ERR	0x4

typedef struct m2m_vec_s {
	dma64addr_t	addr;
	uint32		len;
} m2m_vec_t;

typedef struct m2m_desc_s {
	uint8		num_rx_vec;
	uint8		num_tx_vec;
	uint8		flags;
	bool		commit;
	m2m_vec_t	vec[];
} m2m_desc_t;

#define INIT_M2M_DESC(desc) \
{\
	desc->num_rx_vec = 0;	\
	desc->num_tx_vec = 0;	\
	desc->flags = 0;	\
	desc->commit = TRUE;	\
}

#define SETUP_RX_DESC(desc, rxaddr, rxlen) \
{\
	ASSERT(desc->num_tx_vec == 0);	\
	desc->vec[desc->num_rx_vec].addr = rxaddr;	\
	desc->vec[desc->num_rx_vec].len = rxlen;	\
	desc->num_rx_vec++;	\
}

#define SETUP_TX_DESC(desc, txaddr, txlen) \
{\
	desc->vec[desc->num_tx_vec + desc->num_rx_vec].addr = txaddr;	\
	desc->vec[desc->num_tx_vec + desc->num_rx_vec].len = txlen;	\
	desc->num_tx_vec++;	\
}

#define SETUP_XFER_FLAGS(desc, flag) \
{\
	desc->flags |= flag;	\
}

#define DD_IS_SHARED_POOL(di) ((di)->dmactrlflags & DMA_CTRL_SHARED_POOL)

extern int dma_m2m_submit(hnddma_t *dmah, m2m_desc_t *desc, bool implicit);
extern void dma_chan_enable(hnddma_t *dmah, bool enable);

extern bool dma_rxfill_p(hnddma_t *dmah, void *p);
extern void dma_aqm_di_link(hnddma_t *dmah_aqm, hnddma_t *dmah_hw);
extern void dma_dump_aqminfo(hnddma_t * dmah, struct bcmstrbuf *b, uint16 fifonum);

/* To dump ntxd and nrxd from the DMA ring */
void dma_dump_info(hnddma_t *dmah, uint16 fifonum, struct bcmstrbuf *b);

#endif	/* _hnddma_h_ */
