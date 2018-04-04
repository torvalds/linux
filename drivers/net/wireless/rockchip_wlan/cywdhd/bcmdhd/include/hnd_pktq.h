/*
 * HND generic pktq operation primitives
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
 * $Id$
 */

#ifndef _hnd_pktq_h_
#define _hnd_pktq_h_

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
#define PKTQ_LEN_MAX            0xFFFF  /* Max uint16 65535 packets */
#ifndef PKTQ_LEN_DEFAULT
#define PKTQ_LEN_DEFAULT        128	/* Max 128 packets */
#endif
#ifndef PKTQ_MAX_PREC
#define PKTQ_MAX_PREC           16	/* Maximum precedence levels */
#endif

typedef struct pktq_prec {
	void *head;     /**< first packet to dequeue */
	void *tail;     /**< last packet to dequeue */
	uint16 len;     /**< number of queued packets */
	uint16 max;     /**< maximum number of queued packets */
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

typedef struct {
	uint32                  _prec_log;
	pktq_counters_t*        _prec_cnt[PKTQ_MAX_PREC];     /**< Counters per queue  */
} pktq_log_t;
#endif /* PKTQ_LOG */


#define PKTQ_COMMON	\
	uint16 num_prec;        /**< number of precedences in use */			\
	uint16 hi_prec;         /**< rapid dequeue hint (>= highest non-empty prec) */	\
	uint16 max;             /**< total max packets */					\
	uint16 len;             /**< total number of packets */

/* multi-priority pkt queue */
struct pktq {
	PKTQ_COMMON
	/* q array must be last since # of elements can be either PKTQ_MAX_PREC or 1 */
	struct pktq_prec q[PKTQ_MAX_PREC];
	HND_PKTQ_MUTEX_DECL(mutex)
#ifdef PKTQ_LOG
	pktq_log_t*      pktqlog;
#endif
};

/* simple, non-priority pkt queue */
struct spktq {
	PKTQ_COMMON
	/* q array must be last since # of elements can be either PKTQ_MAX_PREC or 1 */
	struct pktq_prec q[1];
	HND_PKTQ_MUTEX_DECL(mutex)
};

#define PKTQ_PREC_ITER(pq, prec)        for (prec = (pq)->num_prec - 1; prec >= 0; prec--)

/* fn(pkt, arg).  return true if pkt belongs to if */
typedef bool (*ifpkt_cb_t)(void*, int);

/* operations on a specific precedence in packet queue */

#define pktq_psetmax(pq, prec, _max)	((pq)->q[prec].max = (_max))
#define pktq_pmax(pq, prec)		((pq)->q[prec].max)
#define pktq_plen(pq, prec)		((pq)->q[prec].len)
#define pktq_pempty(pq, prec)		((pq)->q[prec].len == 0)
#define pktq_ppeek(pq, prec)		((pq)->q[prec].head)
#define pktq_ppeek_tail(pq, prec)	((pq)->q[prec].tail)
#ifdef HND_PKTQ_THREAD_SAFE
extern int pktq_pavail(struct pktq *pq, int prec);
extern bool pktq_pfull(struct pktq *pq, int prec);
#else
#define pktq_pavail(pq, prec)	((pq)->q[prec].max - (pq)->q[prec].len)
#define pktq_pfull(pq, prec)	((pq)->q[prec].len >= (pq)->q[prec].max)
#endif	/* HND_PKTQ_THREAD_SAFE */

extern void  pktq_append(struct pktq *pq, int prec, struct spktq *list);
extern void  pktq_prepend(struct pktq *pq, int prec, struct spktq *list);

extern void *pktq_penq(struct pktq *pq, int prec, void *p);
extern void *pktq_penq_head(struct pktq *pq, int prec, void *p);
extern void *pktq_pdeq(struct pktq *pq, int prec);
extern void *pktq_pdeq_prev(struct pktq *pq, int prec, void *prev_p);
extern void *pktq_pdeq_with_fn(struct pktq *pq, int prec, ifpkt_cb_t fn, int arg);
extern void *pktq_pdeq_tail(struct pktq *pq, int prec);
/* Empty the queue at particular precedence level */
extern void pktq_pflush(osl_t *osh, struct pktq *pq, int prec, bool dir,
	ifpkt_cb_t fn, int arg);
/* Remove a specified packet from its queue */
extern bool pktq_pdel(struct pktq *pq, void *p, int prec);

/* operations on a set of precedences in packet queue */

extern int pktq_mlen(struct pktq *pq, uint prec_bmp);
extern void *pktq_mdeq(struct pktq *pq, uint prec_bmp, int *prec_out);
extern void *pktq_mpeek(struct pktq *pq, uint prec_bmp, int *prec_out);

/* operations on packet queue as a whole */

#define pktq_len(pq)		((int)(pq)->len)
#define pktq_max(pq)		((int)(pq)->max)
#define pktq_empty(pq)		((pq)->len == 0)
#ifdef HND_PKTQ_THREAD_SAFE
extern int pktq_avail(struct pktq *pq);
extern bool pktq_full(struct pktq *pq);
#else
#define pktq_avail(pq)		((int)((pq)->max - (pq)->len))
#define pktq_full(pq)		((pq)->len >= (pq)->max)
#endif	/* HND_PKTQ_THREAD_SAFE */

/* operations for single precedence queues */
#define pktenq(pq, p)		pktq_penq(((struct pktq *)(void *)pq), 0, (p))
#define pktenq_head(pq, p)	pktq_penq_head(((struct pktq *)(void *)pq), 0, (p))
#define pktdeq(pq)		pktq_pdeq(((struct pktq *)(void *)pq), 0)
#define pktdeq_tail(pq)		pktq_pdeq_tail(((struct pktq *)(void *)pq), 0)
#define pktqflush(osh, pq)	pktq_flush(osh, ((struct pktq *)(void *)pq), TRUE, NULL, 0)
#define pktqinit(pq, len)	pktq_init(((struct pktq *)(void *)pq), 1, len)
#define pktqdeinit(pq)		pktq_deinit((struct pktq *)(void *)pq)
#define pktqavail(pq)		pktq_avail((struct pktq *)(void *)pq)
#define pktqfull(pq)		pktq_full((struct pktq *)(void *)pq)

extern bool pktq_init(struct pktq *pq, int num_prec, int max_len);
extern bool pktq_deinit(struct pktq *pq);

extern void pktq_set_max_plen(struct pktq *pq, int prec, int max_len);

/* prec_out may be NULL if caller is not interested in return value */
extern void *pktq_deq(struct pktq *pq, int *prec_out);
extern void *pktq_deq_tail(struct pktq *pq, int *prec_out);
extern void *pktq_peek(struct pktq *pq, int *prec_out);
extern void *pktq_peek_tail(struct pktq *pq, int *prec_out);
extern void pktq_flush(osl_t *osh, struct pktq *pq, bool dir, ifpkt_cb_t fn, int arg);

#ifdef __cplusplus
	}
#endif

#endif /* _hnd_pktq_h_ */
