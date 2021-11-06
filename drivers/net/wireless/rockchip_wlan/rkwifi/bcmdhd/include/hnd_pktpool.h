/*
 * HND generic packet pool operation primitives
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

#ifndef _hnd_pktpool_h_
#define _hnd_pktpool_h_

#include <typedefs.h>
#include <osl.h>
#include <osl_ext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* mutex macros for thread safe */
#ifdef HND_PKTPOOL_THREAD_SAFE
#define HND_PKTPOOL_MUTEX_DECL(mutex)		OSL_EXT_MUTEX_DECL(mutex)
#else
#define HND_PKTPOOL_MUTEX_DECL(mutex)
#endif

#ifdef BCMPKTPOOL
#define POOL_ENAB(pool)		((pool) && (pool)->inited)
#else /* BCMPKTPOOL */
#define POOL_ENAB(bus)		0
#endif /* BCMPKTPOOL */

#ifndef PKTPOOL_LEN_MAX
#define PKTPOOL_LEN_MAX		40
#endif /* PKTPOOL_LEN_MAX */
#define PKTPOOL_CB_MAX		3
#define PKTPOOL_CB_MAX_AVL	4

/* REMOVE_RXCPLID is an arg for pktpool callback function for removing rxcplID
 * and host addr associated with the rxfrag or shared pool buffer during pktpool_reclaim().
 */
#define REMOVE_RXCPLID            2

#define FREE_ALL_PKTS		0
#define FREE_ALL_FRAG_PKTS	1

/* forward declaration */
struct pktpool;

typedef void (*pktpool_cb_t)(struct pktpool *pool, void *arg);
typedef struct {
	pktpool_cb_t cb;
	void *arg;
	uint8 refcnt;
} pktpool_cbinfo_t;

/** PCIe SPLITRX related: call back fn extension to populate host address in pool pkt */
typedef int (*pktpool_cb_extn_t)(struct pktpool *pool, void *arg1, void* pkt, int arg2,
	uint *pktcnt);
typedef struct {
	pktpool_cb_extn_t cb;
	void *arg;
} pktpool_cbextn_info_t;

#ifdef BCMDBG_POOL
/* pkt pool debug states */
#define POOL_IDLE	0
#define POOL_RXFILL	1
#define POOL_RXDH	2
#define POOL_RXD11	3
#define POOL_TXDH	4
#define POOL_TXD11	5
#define POOL_AMPDU	6
#define POOL_TXENQ	7

typedef struct {
	void *p;
	uint32 cycles;
	uint32 dur;
} pktpool_dbg_t;

typedef struct {
	uint8 txdh;	/* tx to host */
	uint8 txd11;	/* tx to d11 */
	uint8 enq;	/* waiting in q */
	uint8 rxdh;	/* rx from host */
	uint8 rxd11;	/* rx from d11 */
	uint8 rxfill;	/* dma_rxfill */
	uint8 idle;	/* avail in pool */
} pktpool_stats_t;
#endif /* BCMDBG_POOL */

typedef struct pktpool {
	bool inited;            /**< pktpool_init was successful */
	uint8 type;             /**< type of lbuf: basic, frag, etc */
	uint8 id;               /**< pktpool ID:  index in registry */
	bool istx;              /**< direction: transmit or receive data path */
	HND_PKTPOOL_MUTEX_DECL(mutex)	/**< thread-safe mutex */

	void * freelist;        /**< free list: see PKTNEXTFREE(), PKTSETNEXTFREE() */
	uint16 avail;           /**< number of packets in pool's free list */
	uint16 n_pkts;             /**< number of packets managed by pool */
	uint16 maxlen;          /**< maximum size of pool <= PKTPOOL_LEN_MAX */
	uint16 max_pkt_bytes;   /**< size of pkt buffer in [bytes], excluding lbuf|lbuf_frag */

	bool empty;
	uint8 cbtoggle;
	uint8 cbcnt;
	uint8 ecbcnt;
	uint8 emptycb_disable;	/**< Value of type enum pktpool_empty_cb_state */
	pktpool_cbinfo_t *availcb_excl;
	pktpool_cbinfo_t cbs[PKTPOOL_CB_MAX_AVL];
	pktpool_cbinfo_t ecbs[PKTPOOL_CB_MAX];
	pktpool_cbextn_info_t cbext;	/**< PCIe SPLITRX related */
	pktpool_cbextn_info_t rxcplidfn;
	pktpool_cbinfo_t dmarxfill;
	/* variables for pool_heap management */
	uint32 poolheap_flag;
	uint16 poolheap_count;	/* Number of allocation done from this pool */
	uint16 min_backup_buf;	/* Minimum number of buffer that should be kept in pool */
	bool is_heap_pool;	/* Whether this pool can be used as heap */
	bool release_active;
	uint8 mem_handle;
#ifdef BCMDBG_POOL
	uint8 dbg_cbcnt;
	pktpool_cbinfo_t dbg_cbs[PKTPOOL_CB_MAX];
	uint16 dbg_qlen;
	pktpool_dbg_t dbg_q[PKTPOOL_LEN_MAX + 1];
#endif
} pktpool_t;

pktpool_t *get_pktpools_registry(int id);
#define pktpool_get(pktp)	(pktpool_get_ext((pktp), (pktp)->type, NULL))

/* Incarnate a pktpool registry. On success returns total_pools. */
extern int pktpool_attach(osl_t *osh, uint32 total_pools);
extern int pktpool_dettach(osl_t *osh); /* Relinquish registry */

extern int pktpool_init(osl_t *osh, pktpool_t *pktp, int *pktplen, int plen, bool istx, uint8 type,
	bool is_heap_pool, uint32 heap_pool_flag, uint16 min_backup_buf);
extern int pktpool_deinit(osl_t *osh, pktpool_t *pktp);
extern int pktpool_fill(osl_t *osh, pktpool_t *pktp, bool minimal);
extern int pktpool_empty(osl_t *osh, pktpool_t *pktp);
extern uint16 pktpool_reclaim(osl_t *osh, pktpool_t *pktp, uint16 free_cnt, uint8 action);
void pktpool_update_freelist(pktpool_t *pktp, void *p, uint pkts_consumed);
extern void* pktpool_get_ext(pktpool_t *pktp, uint8 type, uint *pktcnt);
extern void pktpool_free(pktpool_t *pktp, void *p);
void pktpool_nfree(pktpool_t *pktp, void *head, void *tail, uint count);
extern int pktpool_add(pktpool_t *pktp, void *p);
extern int pktpool_avail_notify_normal(osl_t *osh, pktpool_t *pktp);
extern int pktpool_avail_notify_exclusive(osl_t *osh, pktpool_t *pktp, pktpool_cb_t cb);
extern int pktpool_avail_register(pktpool_t *pktp, pktpool_cb_t cb, void *arg);
extern int pktpool_avail_deregister(pktpool_t *pktp, pktpool_cb_t cb, void *arg);
extern int pktpool_empty_register(pktpool_t *pktp, pktpool_cb_t cb, void *arg);
extern int pktpool_setmaxlen(pktpool_t *pktp, uint16 max_pkts);
extern int pktpool_setmaxlen_strict(osl_t *osh, pktpool_t *pktp, uint16 max_pkts);
extern void pktpool_emptycb_disable(pktpool_t *pktp, bool disable);
extern bool pktpool_emptycb_disabled(pktpool_t *pktp);
extern int pktpool_hostaddr_fill_register(pktpool_t *pktp, pktpool_cb_extn_t cb, void *arg1);
extern int pktpool_rxcplid_fill_register(pktpool_t *pktp, pktpool_cb_extn_t cb, void *arg);
extern void pktpool_invoke_dmarxfill(pktpool_t *pktp);
extern int pkpool_haddr_avail_register_cb(pktpool_t *pktp, pktpool_cb_t cb, void *arg);
extern int pktpool_avail(pktpool_t *pktpool);

#define POOLPTR(pp)         ((pktpool_t *)(pp))
#define POOLID(pp)          (POOLPTR(pp)->id)

#define POOLSETID(pp, ppid) (POOLPTR(pp)->id = (ppid))

#define pktpool_tot_pkts(pp)  (POOLPTR(pp)->n_pkts)   /**< n_pkts = avail + in_use <= max_pkts */
#define pktpool_max_pkt_bytes(pp)    (POOLPTR(pp)->max_pkt_bytes)
#define pktpool_max_pkts(pp)  (POOLPTR(pp)->maxlen)

/*
 * ----------------------------------------------------------------------------
 * A pool ID is assigned with a pkt pool during pool initialization. This is
 * done by maintaining a registry of all initialized pools, and the registry
 * index at which the pool is registered is used as the pool's unique ID.
 * ID 0 is reserved and is used to signify an invalid pool ID.
 * All packets henceforth allocated from a pool will be tagged with the pool's
 * unique ID. Packets allocated from the heap will use the reserved ID = 0.
 * Packets with non-zero pool id signify that they were allocated from a pool.
 * A maximum of 15 pools are supported, allowing a 4bit pool ID to be used
 * in place of a 32bit pool pointer in each packet.
 * ----------------------------------------------------------------------------
 */
#define PKTPOOL_INVALID_ID          (0)
#define PKTPOOL_MAXIMUM_ID          (15)

/* Registry of pktpool(s) */
/* Pool ID to/from Pool Pointer converters */
#define PKTPOOL_ID2PTR(id)          (get_pktpools_registry(id))
#define PKTPOOL_PTR2ID(pp)          (POOLID(pp))

#ifndef PKTID_POOL
/* max pktids reserved for pktpool is updated properly in Makeconf */
#define PKTID_POOL		    (PKT_MAXIMUM_ID - 32u)
#endif /* PKTID_POOL */
extern uint32 total_pool_pktid_count;

#ifdef BCMDBG_POOL
extern int pktpool_dbg_register(pktpool_t *pktp, pktpool_cb_t cb, void *arg);
extern int pktpool_start_trigger(pktpool_t *pktp, void *p);
extern int pktpool_dbg_dump(pktpool_t *pktp);
extern int pktpool_dbg_notify(pktpool_t *pktp);
extern int pktpool_stats_dump(pktpool_t *pktp, pktpool_stats_t *stats);
#endif /* BCMDBG_POOL */

#ifdef BCMPKTPOOL
#define SHARED_POOL		(pktpool_shared)
extern pktpool_t *pktpool_shared;
#ifdef BCMFRAGPOOL
#define SHARED_FRAG_POOL	(pktpool_shared_lfrag)
extern pktpool_t *pktpool_shared_lfrag;
#endif

#ifdef BCMALFRAGPOOL
#define SHARED_ALFRAG_POOL	(pktpool_shared_alfrag)
extern pktpool_t *pktpool_shared_alfrag;

#define SHARED_ALFRAG_DATA_POOL	(pktpool_shared_alfrag_data)
extern pktpool_t *pktpool_shared_alfrag_data;
#endif

#ifdef BCMRESVFRAGPOOL
#define RESV_FRAG_POOL		(pktpool_resv_lfrag)
#define RESV_POOL_INFO		(resv_pool_info)
#else
#define RESV_FRAG_POOL		((struct pktpool *)NULL)
#define RESV_POOL_INFO		(NULL)
#endif /* BCMRESVFRAGPOOL */

/** PCIe SPLITRX related */
#define SHARED_RXFRAG_POOL	(pktpool_shared_rxlfrag)
extern pktpool_t *pktpool_shared_rxlfrag;

#define SHARED_RXDATA_POOL	(pktpool_shared_rxdata)
extern pktpool_t *pktpool_shared_rxdata;

int hnd_pktpool_init(osl_t *osh);
void hnd_pktpool_deinit(osl_t *osh);
int hnd_pktpool_fill(pktpool_t *pktpool, bool minimal);
void hnd_pktpool_refill(bool minimal);

#ifdef BCMRESVFRAGPOOL
extern pktpool_t *pktpool_resv_lfrag;
extern struct resv_info *resv_pool_info;
#endif /* BCMRESVFRAGPOOL */

/* Current identified use case flags for pool heap manager */
#define POOL_HEAP_FLAG_D3	(1 << 0)
#define POOL_HEAP_FLAG_RSRVPOOL	(1 << 1)

#ifdef POOL_HEAP_RECONFIG
typedef void (*pktpool_heap_cb_t)(void *arg, bool entry);

extern void hnd_pktpool_heap_handle(osl_t *osh, uint32 flag, bool enable);
extern int hnd_pktpool_heap_register_cb(pktpool_heap_cb_t fn, void *ctxt, uint32 flag);
extern int hnd_pktpool_heap_deregister_cb(pktpool_heap_cb_t fn);
extern void *hnd_pktpool_freelist_alloc(uint size, uint alignbits, uint32 flag);
extern uint16 hnd_pktpool_get_min_bkup_buf(pktpool_t *pktp);
#endif /* POOL_HEAP_RECONFIG */
extern uint32 hnd_pktpool_get_total_poolheap_count(void);

#else /* BCMPKTPOOL */
#define SHARED_POOL		((struct pktpool *)NULL)
#endif /* BCMPKTPOOL */

#ifdef __cplusplus
	}
#endif

#endif /* _hnd_pktpool_h_ */
