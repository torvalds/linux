/*
 * HND generic pktq operation primitives
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

#ifndef _hnd_pktq_h_
#define _hnd_pktq_h_

#include <osl.h>
#include <osl_ext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* mutex macros for thread safe */
#ifdef HND_PKTQ_THREAD_SAFE
#define HND_PKTQ_MUTEX_DECL(mutex)		OSL_EXT_MUTEX_DECL(mutex)
#else
#define HND_PKTQ_MUTEX_DECL(mutex)
#endif

/* osl multi-precedence packet queue */
#define PKTQ_LEN_MAX            0xFFFFu  /* Max uint16 65535 packets */
#ifndef PKTQ_LEN_DEFAULT
#define PKTQ_LEN_DEFAULT        128u	/* Max 128 packets */
#endif
#ifndef PKTQ_MAX_PREC
#define PKTQ_MAX_PREC           16	/* Maximum precedence levels */
#endif

/** Queue for a single precedence level */
typedef struct pktq_prec {
	void *head;     /**< first packet to dequeue */
	void *tail;     /**< last packet to dequeue */
	uint16 n_pkts;       /**< number of queued packets */
	uint16 max_pkts;     /**< maximum number of queued packets */
	uint16 stall_count;    /**< # seconds since no packets are dequeued  */
	uint16 dequeue_count;  /**< # of packets dequeued in last 1 second */
} pktq_prec_t;

#ifdef PKTQ_LOG
typedef struct {
	uint32 requested;    /**< packets requested to be stored */
	uint32 stored;	     /**< packets stored */
	uint32 saved;	     /**< packets saved,
	                            because a lowest priority queue has given away one packet
	                      */
	uint32 selfsaved;    /**< packets saved,
	                            because an older packet from the same queue has been dropped
	                      */
	uint32 full_dropped; /**< packets dropped,
	                            because pktq is full with higher precedence packets
	                      */
	uint32 dropped;      /**< packets dropped because pktq per that precedence is full */
	uint32 sacrificed;   /**< packets dropped,
	                            in order to save one from a queue of a highest priority
	                      */
	uint32 busy;         /**< packets droped because of hardware/transmission error */
	uint32 retry;        /**< packets re-sent because they were not received */
	uint32 ps_retry;     /**< packets retried again prior to moving power save mode */
	uint32 suppress;     /**< packets which were suppressed and not transmitted */
	uint32 retry_drop;   /**< packets finally dropped after retry limit */
	uint32 max_avail;    /**< the high-water mark of the queue capacity for packets -
	                            goes to zero as queue fills
	                      */
	uint32 max_used;     /**< the high-water mark of the queue utilisation for packets -
						        increases with use ('inverse' of max_avail)
				          */
	uint32 queue_capacity; /**< the maximum capacity of the queue */
	uint32 rtsfail;        /**< count of rts attempts that failed to receive cts */
	uint32 acked;          /**< count of packets sent (acked) successfully */
	uint32 txrate_succ;    /**< running total of phy rate of packets sent successfully */
	uint32 txrate_main;    /**< running totoal of primary phy rate of all packets */
	uint32 throughput;     /**< actual data transferred successfully */
	uint32 airtime;        /**< cumulative total medium access delay in useconds */
	uint32  _logtime;      /**< timestamp of last counter clear  */
} pktq_counters_t;

#define PKTQ_LOG_COMMON \
	uint32			pps_time;	/**< time spent in ps pretend state */ \
	uint32                  _prec_log;

typedef struct {
	PKTQ_LOG_COMMON
	pktq_counters_t*        _prec_cnt[PKTQ_MAX_PREC];     /**< Counters per queue  */
} pktq_log_t;
#else
typedef struct pktq_log pktq_log_t;
#endif /* PKTQ_LOG */

/** multi-priority packet queue */
struct pktq {
	HND_PKTQ_MUTEX_DECL(mutex)
	pktq_log_t *pktqlog;
	uint16 num_prec;        /**< number of precedences in use */
	uint16 hi_prec;         /**< rapid dequeue hint (>= highest non-empty prec) */
	uint16 max_pkts;        /**< max  packets */
	uint16 n_pkts_tot;      /**< total (cummulative over all precedences) number of packets */
	/* q array must be last since # of elements can be either PKTQ_MAX_PREC or 1 */
	struct pktq_prec q[PKTQ_MAX_PREC];
};

/** simple, non-priority packet queue */
struct spktq {
	HND_PKTQ_MUTEX_DECL(mutex)
	struct pktq_prec q;
};

#define PKTQ_PREC_ITER(pq, prec)        for (prec = (pq)->num_prec - 1; prec >= 0; prec--)

/* fn(pkt, arg).  return true if pkt belongs to bsscfg */
typedef bool (*ifpkt_cb_t)(void*, int);

/*
 * pktq filter support
 */

/** filter function return values */
typedef enum {
	PKT_FILTER_NOACTION = 0,    /**< restore the pkt to its position in the queue */
	PKT_FILTER_DELETE = 1,      /**< delete the pkt */
	PKT_FILTER_REMOVE = 2,      /**< do not restore the pkt to the queue,
	                             *   filter fn has taken ownership of the pkt
	                             */
} pktq_filter_result_t;

/**
 * Caller supplied filter function to pktq_pfilter(), pktq_filter().
 * Function filter(ctx, pkt) is called with its ctx pointer on each pkt in the
 * pktq.  When the filter function is called, the supplied pkt will have been
 * unlinked from the pktq.  The filter function returns a pktq_filter_result_t
 * result specifying the action pktq_filter()/pktq_pfilter() should take for
 * the pkt.
 * Here are the actions taken by pktq_filter/pfilter() based on the supplied
 * filter function's return value:
 *
 * PKT_FILTER_NOACTION - The filter will re-link the pkt at its
 *     previous location.
 *
 * PKT_FILTER_DELETE - The filter will not relink the pkt and will
 *     call the user supplied defer_free_pkt fn on the packet.
 *
 * PKT_FILTER_REMOVE - The filter will not relink the pkt. The supplied
 *     filter fn took ownership (or deleted) the pkt.
 *
 * WARNING: pkts inserted by the user (in pkt_filter and/or flush callbacks
 * and chains) in the prec queue will not be seen by the filter, and the prec
 * queue will be temporarily be removed from the queue hence there're side
 * effects including pktq_n_pkts_tot() on the queue won't reflect the correct number
 * of packets in the queue.
 */

typedef pktq_filter_result_t (*pktq_filter_t)(void* ctx, void* pkt);

/**
 * The defer_free_pkt callback is invoked when the the pktq_filter callback
 * returns PKT_FILTER_DELETE decision, which allows the user to deposite
 * the packet appropriately based on the situation (free the packet or
 * save it in a temporary queue etc.).
 */
typedef void (*defer_free_pkt_fn_t)(void *ctx, void *pkt);

/**
 * The flush_free_pkt callback is invoked when all packets in the pktq
 * are processed.
 */
typedef void (*flush_free_pkt_fn_t)(void *ctx);

#if defined(PROP_TXSTATUS)
/* this callback will be invoked when in low_txq_scb flush()
 *  two back-to-back pkts has same epoch value.
 */
typedef void (*flip_epoch_t)(void *ctx, void *pkt, uint8 *flipEpoch, uint8 *lastEpoch);
#endif /* defined(PROP_TXSTATUS) */

/** filter a pktq, using the caller supplied filter/deposition/flush functions */
extern void  pktq_filter(struct pktq *pq, pktq_filter_t fn, void* arg,
	defer_free_pkt_fn_t defer, void *defer_ctx, flush_free_pkt_fn_t flush, void *flush_ctx);
/** filter a particular precedence in pktq, using the caller supplied filter function */
extern void  pktq_pfilter(struct pktq *pq, int prec, pktq_filter_t fn, void* arg,
	defer_free_pkt_fn_t defer, void *defer_ctx, flush_free_pkt_fn_t flush, void *flush_ctx);
/** filter a simple non-precedence in spktq, using the caller supplied filter function */
extern void spktq_filter(struct spktq *spq, pktq_filter_t fltr, void* fltr_ctx,
	defer_free_pkt_fn_t defer, void *defer_ctx, flush_free_pkt_fn_t flush, void *flush_ctx);

/* operations on a specific precedence in packet queue */
#define pktqprec_max_pkts(pq, prec)		((pq)->q[prec].max_pkts)
#define pktqprec_n_pkts(pq, prec)		((pq)->q[prec].n_pkts)
#define pktqprec_empty(pq, prec)		((pq)->q[prec].n_pkts == 0)
#define pktqprec_peek(pq, prec)			((pq)->q[prec].head)
#define pktqprec_peek_tail(pq, prec)	((pq)->q[prec].tail)
#define spktq_peek_tail(pq)		((pq)->q.tail)
#ifdef HND_PKTQ_THREAD_SAFE
extern int pktqprec_avail_pkts(struct pktq *pq, int prec);
extern bool pktqprec_full(struct pktq *pq, int prec);
#else
#define pktqprec_avail_pkts(pq, prec)	((pq)->q[prec].max_pkts - (pq)->q[prec].n_pkts)
#define pktqprec_full(pq, prec)	((pq)->q[prec].n_pkts >= (pq)->q[prec].max_pkts)
#endif	/* HND_PKTQ_THREAD_SAFE */

extern void  pktq_append(struct pktq *pq, int prec, struct spktq *list);
extern void  spktq_append(struct spktq *spq, struct spktq *list);
extern void  pktq_prepend(struct pktq *pq, int prec, struct spktq *list);
extern void  spktq_prepend(struct spktq *spq, struct spktq *list);
extern void *pktq_penq(struct pktq *pq, int prec, void *p);
extern void *pktq_penq_head(struct pktq *pq, int prec, void *p);
extern void *pktq_pdeq(struct pktq *pq, int prec);
extern void *pktq_pdeq_prev(struct pktq *pq, int prec, void *prev_p);
extern void *pktq_pdeq_with_fn(struct pktq *pq, int prec, ifpkt_cb_t fn, int arg);
extern void *pktq_pdeq_tail(struct pktq *pq, int prec);
/** Remove a specified packet from its queue */
extern bool pktq_pdel(struct pktq *pq, void *p, int prec);

/* For single precedence queues */
extern void *spktq_enq_chain(struct spktq *dspq, struct spktq *sspq);
extern void *spktq_enq(struct spktq *spq, void *p);
extern void *spktq_enq_head(struct spktq *spq, void *p);
extern void *spktq_deq(struct spktq *spq);
extern void *spktq_deq_virt(struct spktq *spq);
extern void *spktq_deq_tail(struct spktq *spq);

/* operations on a set of precedences in packet queue */

extern int pktq_mlen(struct pktq *pq, uint prec_bmp);
extern void *pktq_mdeq(struct pktq *pq, uint prec_bmp, int *prec_out);
extern void *pktq_mpeek(struct pktq *pq, uint prec_bmp, int *prec_out);

/* operations on packet queue as a whole */

#define pktq_n_pkts_tot(pq)	((int)(pq)->n_pkts_tot)
#define pktq_max(pq)		((int)(pq)->max_pkts)
#define pktq_empty(pq)		((pq)->n_pkts_tot == 0)
#define spktq_n_pkts(spq)	((int)(spq)->q.n_pkts)
#define spktq_empty(spq)	((spq)->q.n_pkts == 0)

#define spktq_max(spq)		((int)(spq)->q.max_pkts)
#define spktq_empty(spq)	((spq)->q.n_pkts == 0)
#ifdef HND_PKTQ_THREAD_SAFE
extern int pktq_avail(struct pktq *pq);
extern bool pktq_full(struct pktq *pq);
extern int spktq_avail(struct spktq *spq);
extern bool spktq_full(struct spktq *spq);
#else
#define pktq_avail(pq)		((int)((pq)->max_pkts - (pq)->n_pkts_tot))
#define pktq_full(pq)		((pq)->n_pkts_tot >= (pq)->max_pkts)
#define spktq_avail(spq)	((int)((spq)->q.max_pkts - (spq)->q.n_pkts))
#define spktq_full(spq)		((spq)->q.n_pkts >= (spq)->q.max_pkts)
#endif	/* HND_PKTQ_THREAD_SAFE */

/* operations for single precedence queues */
#define pktenq(pq, p)		pktq_penq((pq), 0, (p))
#define pktenq_head(pq, p)	pktq_penq_head((pq), 0, (p))
#define pktdeq(pq)		pktq_pdeq((pq), 0)
#define pktdeq_tail(pq)		pktq_pdeq_tail((pq), 0)
#define pktqflush(osh, pq, dir)	pktq_pflush(osh, (pq), 0, (dir))
#define pktqinit(pq, max_pkts)	pktq_init((pq), 1, (max_pkts))
#define pktqdeinit(pq)		pktq_deinit((pq))
#define pktqavail(pq)		pktq_avail((pq))
#define pktqfull(pq)		pktq_full((pq))
#define pktqfilter(pq, fltr, fltr_ctx, defer, defer_ctx, flush, flush_ctx) \
	pktq_pfilter((pq), 0, (fltr), (fltr_ctx), (defer), (defer_ctx), (flush), (flush_ctx))

/* operations for simple non-precedence queues */
#define spktenq(spq, p)			spktq_enq((spq), (p))
#define spktenq_head(spq, p)		spktq_enq_head((spq), (p))
#define spktdeq(spq)			spktq_deq((spq))
#define spktdeq_tail(spq)		spktq_deq_tail((spq))
#define spktqflush(osh, spq, dir)	spktq_flush((osh), (spq), (dir))
#define spktqinit(spq, max_pkts)	spktq_init((spq), (max_pkts))
#define spktqdeinit(spq)		spktq_deinit((spq))
#define spktqavail(spq)			spktq_avail((spq))
#define spktqfull(spq)			spktq_full((spq))

#define spktqfilter(spq, fltr, fltr_ctx, defer, defer_ctx, flush, flush_ctx) \
	spktq_filter((spq), (fltr), (fltr_ctx), (defer), (defer_ctx), (flush), (flush_ctx))
extern bool pktq_init(struct pktq *pq, int num_prec, uint max_pkts);
extern bool pktq_deinit(struct pktq *pq);
extern bool spktq_init(struct spktq *spq, uint max_pkts);
extern bool spktq_init_list(struct spktq *spq, uint max_pkts,
	void *head, void *tail, uint16 n_pkts);
extern bool spktq_deinit(struct spktq *spq);

extern void pktq_set_max_plen(struct pktq *pq, int prec, uint max_pkts);

/* prec_out may be NULL if caller is not interested in return value */
extern void *pktq_deq(struct pktq *pq, int *prec_out);
extern void *pktq_deq_tail(struct pktq *pq, int *prec_out);
extern void *pktq_peek(struct pktq *pq, int *prec_out);
extern void *spktq_peek(struct spktq *spq);
extern void *pktq_peek_tail(struct pktq *pq, int *prec_out);

/** flush pktq */
extern void pktq_flush(osl_t *osh, struct pktq *pq, bool dir);
/* single precedence queue with callback before deleting a packet */
extern void spktq_flush_ext(osl_t *osh, struct spktq *spq, bool dir,
	void (*pktq_flush_cb)(void *ctx, void *pkt), void *pktq_flush_ctx);
/* single precedence queue */
#define spktq_flush(osh, spq, dir) spktq_flush_ext(osh, spq, dir, NULL, NULL)
/** Empty the queue at particular precedence level */
extern void pktq_pflush(osl_t *osh, struct pktq *pq, int prec, bool dir);

typedef void (*spktq_cb_t)(void *arg, struct spktq *spq);
extern void spktq_free_register(spktq_cb_t cb, void *arg);
extern void spktq_cb(void *spq);
#define SPKTQFREE	spktq_cb

#ifdef __cplusplus
}
#endif

#endif /* _hnd_pktq_h_ */
