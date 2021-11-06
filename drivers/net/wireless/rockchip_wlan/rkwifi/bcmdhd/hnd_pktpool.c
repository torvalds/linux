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

#include <typedefs.h>
#include <osl.h>
#include <osl_ext.h>
#include <bcmutils.h>
#include <wlioctl.h>
#include <hnd_pktpool.h>
#ifdef BCMRESVFRAGPOOL
#include <hnd_resvpool.h>
#endif /* BCMRESVFRAGPOOL */
#ifdef BCMFRWDPOOLREORG
#include <hnd_poolreorg.h>
#endif /* BCMFRWDPOOLREORG */

#if defined(DONGLEBUILD) && defined(SRMEM)
#include <hndsrmem.h>
#endif /* DONGLEBUILD && SRMEM */
#if defined(DONGLEBUILD)
#include <d11_cfg.h>
#endif

/* mutex macros for thread safe */
#ifdef HND_PKTPOOL_THREAD_SAFE
#define HND_PKTPOOL_MUTEX_CREATE(name, mutex)	osl_ext_mutex_create(name, mutex)
#define HND_PKTPOOL_MUTEX_DELETE(mutex)		osl_ext_mutex_delete(mutex)
#define HND_PKTPOOL_MUTEX_ACQUIRE(mutex, msec)	osl_ext_mutex_acquire(mutex, msec)
#define HND_PKTPOOL_MUTEX_RELEASE(mutex)	osl_ext_mutex_release(mutex)
#else
#define HND_PKTPOOL_MUTEX_CREATE(name, mutex)	OSL_EXT_SUCCESS
#define HND_PKTPOOL_MUTEX_DELETE(mutex)		OSL_EXT_SUCCESS
#define HND_PKTPOOL_MUTEX_ACQUIRE(mutex, msec)	OSL_EXT_SUCCESS
#define HND_PKTPOOL_MUTEX_RELEASE(mutex)	OSL_EXT_SUCCESS
#endif

/* Registry size is one larger than max pools, as slot #0 is reserved */
#define PKTPOOLREG_RSVD_ID				(0U)
#define PKTPOOLREG_RSVD_PTR				(POOLPTR(0xdeaddead))
#define PKTPOOLREG_FREE_PTR				(POOLPTR(NULL))

#define PKTPOOL_REGISTRY_SET(id, pp)	(pktpool_registry_set((id), (pp)))
#define PKTPOOL_REGISTRY_CMP(id, pp)	(pktpool_registry_cmp((id), (pp)))

/* Tag a registry entry as free for use */
#define PKTPOOL_REGISTRY_CLR(id)		\
		PKTPOOL_REGISTRY_SET((id), PKTPOOLREG_FREE_PTR)
#define PKTPOOL_REGISTRY_ISCLR(id)		\
		(PKTPOOL_REGISTRY_CMP((id), PKTPOOLREG_FREE_PTR))

/* Tag registry entry 0 as reserved */
#define PKTPOOL_REGISTRY_RSV()			\
		PKTPOOL_REGISTRY_SET(PKTPOOLREG_RSVD_ID, PKTPOOLREG_RSVD_PTR)
#define PKTPOOL_REGISTRY_ISRSVD()		\
		(PKTPOOL_REGISTRY_CMP(PKTPOOLREG_RSVD_ID, PKTPOOLREG_RSVD_PTR))

/* Walk all un-reserved entries in registry */
#define PKTPOOL_REGISTRY_FOREACH(id)	\
		for ((id) = 1U; (id) <= pktpools_max; (id)++)

enum pktpool_empty_cb_state {
	EMPTYCB_ENABLED = 0,	/* Enable callback when new packets are added to pool */
	EMPTYCB_DISABLED,	/* Disable callback when new packets are added to pool */
	EMPTYCB_SKIPPED		/* Packet was added to pool when callback was disabled */
};

uint32 pktpools_max = 0U; /* maximum number of pools that may be initialized */
pktpool_t *pktpools_registry[PKTPOOL_MAXIMUM_ID + 1]; /* Pktpool registry */

/* number of pktids that are reserved for pktpool usage at the moment
 * initializing this with max pktids reserved for pktpool
 * pktpool_init, pktpool_fill and pktpool_refill decrements this
 * pktpool_reclaim, pktpool_empty and heap_pkt_release increments this
 */
#ifdef DONGLEBUILD
uint32 total_pool_pktid_count = PKTID_POOL;
#else
uint32 total_pool_pktid_count = 0U;
#endif /* DONGLEBUILD */

#ifdef POOL_HEAP_RECONFIG
typedef struct pktpool_heap_cb_reg {
	pktpool_heap_cb_t fn;
	void *ctxt;
	uint32 flag;
} pktpool_heap_cb_reg_t;
#define PKTPOOL_MAX_HEAP_CB	2
pktpool_heap_cb_reg_t	pktpool_heap_cb_reg[PKTPOOL_MAX_HEAP_CB];
uint32 pktpool_heap_rel_active = 0U;

static void hnd_pktpool_heap_pkt_release(osl_t *osh, pktpool_t *pktp, uint32 flag);
static void hnd_pktpool_heap_pkt_retrieve(pktpool_t *pktp, uint32 flag);
static int hnd_pktpool_heap_get_cb(uint8 handle, void *ctxt, void *pkt, uint pktsize);
static void hnd_pktpool_lbuf_free_cb(uint8 poolid);
static pktpool_heap_cb_reg_t *BCMRAMFN(hnd_pool_get_cb_registry)(void);
#endif /* POOL_HEAP_RECONFIG */

/* Register/Deregister a pktpool with registry during pktpool_init/deinit */
static int pktpool_register(pktpool_t * poolptr);
static int pktpool_deregister(pktpool_t * poolptr);

/** add declaration */
static void pktpool_avail_notify(pktpool_t *pktp);

/** accessor functions required when ROMming this file, forced into RAM */

pktpool_t *
BCMPOSTTRAPRAMFN(get_pktpools_registry)(int id)
{
	return pktpools_registry[id];
}

static void
BCMRAMFN(pktpool_registry_set)(int id, pktpool_t *pp)
{
	pktpools_registry[id] = pp;
}

static bool
BCMRAMFN(pktpool_registry_cmp)(int id, pktpool_t *pp)
{
	return pktpools_registry[id] == pp;
}

/** Constructs a pool registry to serve a maximum of total_pools */
int
BCMATTACHFN(pktpool_attach)(osl_t *osh, uint32 total_pools)
{
	uint32 poolid;
	BCM_REFERENCE(osh);

	if (pktpools_max != 0U) {
		return BCME_ERROR;
	}

	ASSERT(total_pools <= PKTPOOL_MAXIMUM_ID);

	/* Initialize registry: reserve slot#0 and tag others as free */
	PKTPOOL_REGISTRY_RSV();		/* reserve slot#0 */

	PKTPOOL_REGISTRY_FOREACH(poolid) {	/* tag all unreserved entries as free */
		PKTPOOL_REGISTRY_CLR(poolid);
	}

	pktpools_max = total_pools;

	return (int)pktpools_max;
}

/** Destructs the pool registry. Ascertain all pools were first de-inited */
int
BCMATTACHFN(pktpool_dettach)(osl_t *osh)
{
	uint32 poolid;
	BCM_REFERENCE(osh);

	if (pktpools_max == 0U) {
		return BCME_OK;
	}

	/* Ascertain that no pools are still registered */
	ASSERT(PKTPOOL_REGISTRY_ISRSVD()); /* assert reserved slot */

	PKTPOOL_REGISTRY_FOREACH(poolid) {	/* ascertain all others are free */
		ASSERT(PKTPOOL_REGISTRY_ISCLR(poolid));
	}

	pktpools_max = 0U; /* restore boot state */

	return BCME_OK;
}

/** Registers a pool in a free slot; returns the registry slot index */
static int
BCMATTACHFN(pktpool_register)(pktpool_t * poolptr)
{
	uint32 poolid;

	if (pktpools_max == 0U) {
		return PKTPOOL_INVALID_ID; /* registry has not yet been constructed */
	}

	ASSERT(pktpools_max != 0U);

	/* find an empty slot in pktpools_registry */
	PKTPOOL_REGISTRY_FOREACH(poolid) {
		if (PKTPOOL_REGISTRY_ISCLR(poolid)) {
			PKTPOOL_REGISTRY_SET(poolid, POOLPTR(poolptr)); /* register pool */
			return (int)poolid; /* return pool ID */
		}
	} /* FOREACH */

	return PKTPOOL_INVALID_ID;	/* error: registry is full */
}

/** Deregisters a pktpool, given the pool pointer; tag slot as free */
static int
BCMATTACHFN(pktpool_deregister)(pktpool_t * poolptr)
{
	uint32 poolid;

	ASSERT(POOLPTR(poolptr) != POOLPTR(NULL));

	poolid = POOLID(poolptr);
	ASSERT(poolid <= pktpools_max);

	/* Asertain that a previously registered poolptr is being de-registered */
	if (PKTPOOL_REGISTRY_CMP(poolid, POOLPTR(poolptr))) {
		PKTPOOL_REGISTRY_CLR(poolid); /* mark as free */
	} else {
		ASSERT(0);
		return BCME_ERROR; /* mismatch in registry */
	}

	return BCME_OK;
}

/**
 * pktpool_init:
 * User provides a pktpool_t structure and specifies the number of packets to
 * be pre-filled into the pool (n_pkts).
 * pktpool_init first attempts to register the pool and fetch a unique poolid.
 * If registration fails, it is considered an BCME_ERR, caused by either the
 * registry was not pre-created (pktpool_attach) or the registry is full.
 * If registration succeeds, then the requested number of packets will be filled
 * into the pool as part of initialization. In the event that there is no
 * available memory to service the request, then BCME_NOMEM will be returned
 * along with the count of how many packets were successfully allocated.
 * In dongle builds, prior to memory reclaimation, one should limit the number
 * of packets to be allocated during pktpool_init and fill the pool up after
 * reclaim stage.
 *
 * @param n_pkts           Number of packets to be pre-filled into the pool
 * @param max_pkt_bytes   The size of all packets in a pool must be the same. E.g. PKTBUFSZ.
 * @param type            e.g. 'lbuf_frag'
 */
int
BCMATTACHFN(pktpool_init)(osl_t *osh,
	pktpool_t *pktp,
	int *n_pkts,
	int max_pkt_bytes,
	bool istx,
	uint8 type,
	bool is_heap_pool,
	uint32 heap_pool_flag,
	uint16 min_backup_buf)
{
	int i, err = BCME_OK;
	int pktplen;
	uint8 pktp_id;

	ASSERT(pktp != NULL);
	ASSERT(osh != NULL);
	ASSERT(n_pkts != NULL);

	pktplen = *n_pkts;

	bzero(pktp, sizeof(pktpool_t));

	/* assign a unique pktpool id */
	if ((pktp_id = (uint8) pktpool_register(pktp)) == PKTPOOL_INVALID_ID) {
		return BCME_ERROR;
	}
	POOLSETID(pktp, pktp_id);

	pktp->inited = TRUE;
	pktp->istx = istx ? TRUE : FALSE;
	pktp->max_pkt_bytes = (uint16)max_pkt_bytes;
	pktp->type = type;

#ifdef POOL_HEAP_RECONFIG
	pktp->poolheap_flag = heap_pool_flag;
	pktp->poolheap_count = 0;
	pktp->min_backup_buf = min_backup_buf;
	if (is_heap_pool) {
		if (rte_freelist_mgr_register(&pktp->mem_handle,
			hnd_pktpool_heap_get_cb,
			lb_get_pktalloclen(type, max_pkt_bytes),
			pktp) != BCME_OK) {
			return BCME_ERROR;
		}
	}
	pktp->is_heap_pool = is_heap_pool;
#endif
	if (HND_PKTPOOL_MUTEX_CREATE("pktpool", &pktp->mutex) != OSL_EXT_SUCCESS) {
		return BCME_ERROR;
	}

	pktp->maxlen = PKTPOOL_LEN_MAX;
	pktplen = LIMIT_TO_MAX(pktplen, pktp->maxlen);

	for (i = 0; i < pktplen; i++) {
		void *p;
#ifdef _RTE_
		/* For rte builds, use PKTALLOC rather than PKTGET
		 * Avoid same pkts being dequed and enqued to pool
		 * when allocation fails.
		 */
		p = PKTALLOC(osh, max_pkt_bytes, type);
#else
		p = PKTGET(osh, max_pkt_bytes, TRUE);
#endif

		if (p == NULL) {
			/* Not able to allocate all requested pkts
			 * so just return what was actually allocated
			 * We can add to the pool later
			 */
			if (pktp->freelist == NULL) /* pktpool free list is empty */
				err = BCME_NOMEM;

			goto exit;
		}

		PKTSETPOOL(osh, p, TRUE, pktp); /* Tag packet with pool ID */

		PKTSETFREELIST(p, pktp->freelist); /* insert p at head of free list */
		pktp->freelist = p;

		pktp->avail++;

		ASSERT(total_pool_pktid_count > 0);
		total_pool_pktid_count--;

#ifdef BCMDBG_POOL
		pktp->dbg_q[pktp->dbg_qlen++].p = p;
#endif
	}

exit:
	pktp->n_pkts = pktp->avail;

	*n_pkts = pktp->n_pkts; /* number of packets managed by pool */
	return err;
} /* pktpool_init */

/**
 * pktpool_deinit:
 * Prior to freeing a pktpool, all packets must be first freed into the pktpool.
 * Upon pktpool_deinit, all packets in the free pool will be freed to the heap.
 * An assert is in place to ensure that there are no packets still lingering
 * around. Packets freed to a pool after the deinit will cause a memory
 * corruption as the pktpool_t structure no longer exists.
 */
int
BCMATTACHFN(pktpool_deinit)(osl_t *osh, pktpool_t *pktp)
{
	uint16 freed = 0;

	ASSERT(osh != NULL);
	ASSERT(pktp != NULL);

#ifdef BCMDBG_POOL
	{
		int i;
		for (i = 0; i <= pktp->n_pkts; i++) {
			pktp->dbg_q[i].p = NULL;
		}
	}
#endif

	while (pktp->freelist != NULL) {
		void * p = pktp->freelist;

		pktp->freelist = PKTFREELIST(p); /* unlink head packet from free list */
		PKTSETFREELIST(p, NULL);

		PKTSETPOOL(osh, p, FALSE, NULL); /* clear pool ID tag in pkt */

		total_pool_pktid_count++;
		PKTFREE(osh, p, pktp->istx); /* free the packet */

		freed++;
		ASSERT(freed <= pktp->n_pkts);
	}

	pktp->avail -= freed;
	ASSERT(pktp->avail == 0);

	pktp->n_pkts -= freed;

	pktpool_deregister(pktp); /* release previously acquired unique pool id */
	POOLSETID(pktp, PKTPOOL_INVALID_ID);

	if (HND_PKTPOOL_MUTEX_DELETE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	pktp->inited = FALSE;

	/* Are there still pending pkts? */
	ASSERT(pktp->n_pkts == 0);

	return 0;
}

int
pktpool_fill(osl_t *osh, pktpool_t *pktp, bool minimal)
{
	void *p;
	int err = 0;
	int n_pkts, psize, maxlen;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

#ifdef BCMRXDATAPOOL
	ASSERT((pktp->max_pkt_bytes != 0) || (pktp->type == lbuf_rxfrag));
#else
	ASSERT(pktp->max_pkt_bytes != 0);
#endif /* BCMRXDATAPOOL */

	maxlen = pktp->maxlen;
	psize = minimal ? (maxlen >> 2) : maxlen;
	n_pkts = (int)pktp->n_pkts;
#ifdef POOL_HEAP_RECONFIG
	/*
	 * Consider the packets released to freelist mgr also
	 * as part of pool size
	 */
	n_pkts += pktp->is_heap_pool ?
		pktp->poolheap_count : 0;
#endif
	for (; n_pkts < psize; n_pkts++) {

#ifdef _RTE_
		/* For rte builds, use PKTALLOC rather than PKTGET
		 * Avoid same pkts being dequed and enqued to pool when allocation fails.
		 * All pkts in pool have same length.
		 */
		p = PKTALLOC(osh, pktp->max_pkt_bytes, pktp->type);
#else
		p = PKTGET(osh, pktp->n_pkts, TRUE);
#endif

		if (p == NULL) {
			err = BCME_NOMEM;
			break;
		}

		if (pktpool_add(pktp, p) != BCME_OK) {
			PKTFREE(osh, p, FALSE);
			err = BCME_ERROR;
			break;
		}
		ASSERT(total_pool_pktid_count > 0);
		total_pool_pktid_count--;
	}

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	if (pktp->cbcnt) {
		if (pktp->empty == FALSE)
			pktpool_avail_notify(pktp);
	}

	return err;
}

#ifdef BCMPOOLRECLAIM
/* New API to decrease the pkts from pool, but not deinit
*/
uint16
pktpool_reclaim(osl_t *osh, pktpool_t *pktp, uint16 free_cnt, uint8 action)
{
	uint16 freed = 0;

	pktpool_cb_extn_t cb = NULL;
	void *arg = NULL;
	void *rem_list_head = NULL;
	void *rem_list_tail = NULL;
	bool dont_free = FALSE;

	ASSERT(osh != NULL);
	ASSERT(pktp != NULL);

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS) {
		return freed;
	}

	if (pktp->avail < free_cnt) {
		free_cnt = pktp->avail;
	}

	if (BCMSPLITRX_ENAB() && (pktp->type == lbuf_rxfrag)) {
		/* If pool is shared rx frag pool, use call back fn to reclaim host address
		 * and Rx cpl ID associated with the pkt.
		 */
		ASSERT(pktp->cbext.cb != NULL);

		cb = pktp->cbext.cb;
		arg = pktp->cbext.arg;

	} else if ((pktp->type == lbuf_basic) && (pktp->rxcplidfn.cb != NULL)) {
		/* If pool is shared rx pool, use call back fn to freeup Rx cpl ID
		 * associated with the pkt.
		 */
		cb = pktp->rxcplidfn.cb;
		arg = pktp->rxcplidfn.arg;
	}

	while ((pktp->freelist != NULL) && (free_cnt)) {
		void * p = pktp->freelist;

		pktp->freelist = PKTFREELIST(p); /* unlink head packet from free list */
		PKTSETFREELIST(p, NULL);

		dont_free = FALSE;

		if (action == FREE_ALL_FRAG_PKTS) {
			/* Free lbufs which are marked as frag_free_mem */
			if (!PKTISFRMFRAG(p)) {
				dont_free = TRUE;
			}
		}

		if (dont_free) {
			if (rem_list_head == NULL) {
				rem_list_head = p;
			} else {
				PKTSETFREELIST(rem_list_tail, p);
			}
			rem_list_tail = p;
			continue;
		}
		if (cb != NULL) {
			if (cb(pktp, arg, p, REMOVE_RXCPLID, NULL)) {
				PKTSETFREELIST(p, pktp->freelist);
				pktp->freelist = p;
				break;
			}
		}

		PKTSETPOOL(osh, p, FALSE, NULL); /* clear pool ID tag in pkt */

		pktp->avail--;
		pktp->n_pkts--;

		total_pool_pktid_count++;
		PKTFREE(osh, p, pktp->istx); /* free the packet */

		freed++;
		free_cnt--;
	}

	if (rem_list_head) {
		PKTSETFREELIST(rem_list_tail, pktp->freelist);
		pktp->freelist = rem_list_head;
	}

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS) {
		return freed;
	}

	return freed;
}
#endif /* #ifdef BCMPOOLRECLAIM */

/* New API to empty the pkts from pool, but not deinit
* NOTE: caller is responsible to ensure,
*	all pkts are available in pool for free; else LEAK !
*/
int
pktpool_empty(osl_t *osh, pktpool_t *pktp)
{
	uint16 freed = 0;

	ASSERT(osh != NULL);
	ASSERT(pktp != NULL);

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

#ifdef BCMDBG_POOL
	{
		int i;
		for (i = 0; i <= pktp->n_pkts; i++) {
			pktp->dbg_q[i].p = NULL;
		}
	}
#endif

	while (pktp->freelist != NULL) {
		void * p = pktp->freelist;

		pktp->freelist = PKTFREELIST(p); /* unlink head packet from free list */
		PKTSETFREELIST(p, NULL);

		PKTSETPOOL(osh, p, FALSE, NULL); /* clear pool ID tag in pkt */

		total_pool_pktid_count++;
		PKTFREE(osh, p, pktp->istx); /* free the packet */

		freed++;
		ASSERT(freed <= pktp->n_pkts);
	}

	pktp->avail -= freed;
	ASSERT(pktp->avail == 0);

	pktp->n_pkts -= freed;

	ASSERT(pktp->n_pkts == 0);

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return 0;
}

int
BCMPOSTTRAPFN(pktpool_avail)(pktpool_t *pktpool)
{
	int avail = pktpool->avail;

	if (avail == 0) {
		pktpool_emptycb_disable(pktpool, FALSE);
	}

	return avail;
}

static void *
BCMPOSTTRAPFASTPATH(pktpool_deq)(pktpool_t *pktp)
{
	void *p = NULL;

	if (pktp->avail == 0)
		return NULL;

	ASSERT_FP(pktp->freelist != NULL);

	p = pktp->freelist;  /* dequeue packet from head of pktpool free list */
	pktp->freelist = PKTFREELIST(p); /* free list points to next packet */

#if defined(DONGLEBUILD) && defined(SRMEM)
	if (SRMEM_ENAB()) {
		PKTSRMEM_INC_INUSE(p);
	}
#endif /* DONGLEBUILD && SRMEM */

	PKTSETFREELIST(p, NULL);

	pktp->avail--;

	return p;
}

static void
BCMPOSTTRAPFASTPATH(pktpool_enq)(pktpool_t *pktp, void *p)
{
	ASSERT_FP(p != NULL);

	PKTSETFREELIST(p, pktp->freelist); /* insert at head of pktpool free list */
	pktp->freelist = p; /* free list points to newly inserted packet */

#if defined(DONGLEBUILD) && defined(SRMEM)
	if (SRMEM_ENAB()) {
		PKTSRMEM_DEC_INUSE(p);
	}
#endif /* DONGLEBUILD && SRMEM */

	pktp->avail++;
	ASSERT_FP(pktp->avail <= pktp->n_pkts);
}

/** utility for registering host addr fill function called from pciedev */
int
BCMATTACHFN(pktpool_hostaddr_fill_register)(pktpool_t *pktp, pktpool_cb_extn_t cb, void *arg)
{

	ASSERT(cb != NULL);

	ASSERT(pktp->cbext.cb == NULL);
	pktp->cbext.cb = cb;
	pktp->cbext.arg = arg;
	return 0;
}

int
BCMATTACHFN(pktpool_rxcplid_fill_register)(pktpool_t *pktp, pktpool_cb_extn_t cb, void *arg)
{

	ASSERT(cb != NULL);

	if (pktp == NULL)
		return BCME_ERROR;
	ASSERT(pktp->rxcplidfn.cb == NULL);
	pktp->rxcplidfn.cb = cb;
	pktp->rxcplidfn.arg = arg;
	return 0;
}

/** whenever host posts rxbuffer, invoke dma_rxfill from pciedev layer */
void
pktpool_invoke_dmarxfill(pktpool_t *pktp)
{
	ASSERT(pktp->dmarxfill.cb);
	ASSERT(pktp->dmarxfill.arg);

	if (pktp->dmarxfill.cb)
		pktp->dmarxfill.cb(pktp, pktp->dmarxfill.arg);
}

/** Registers callback functions for split rx mode */
int
BCMATTACHFN(pkpool_haddr_avail_register_cb)(pktpool_t *pktp, pktpool_cb_t cb, void *arg)
{

	ASSERT(cb != NULL);

	pktp->dmarxfill.cb = cb;
	pktp->dmarxfill.arg = arg;

	return 0;
}

/**
 * Registers callback functions.
 * No BCMATTACHFN as it is used in xdc_enable_ep which is not an attach function
 */
int
pktpool_avail_register(pktpool_t *pktp, pktpool_cb_t cb, void *arg)
{
	int err = 0;
	int i;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	ASSERT(cb != NULL);

	for (i = 0; i < pktp->cbcnt; i++) {
		ASSERT(pktp->cbs[i].cb != NULL);
		if ((cb == pktp->cbs[i].cb) && (arg == pktp->cbs[i].arg)) {
			pktp->cbs[i].refcnt++;
			goto done;
		}
	}

	i = pktp->cbcnt;
	if (i == PKTPOOL_CB_MAX_AVL) {
		err = BCME_ERROR;
		goto done;
	}

	ASSERT(pktp->cbs[i].cb == NULL);
	pktp->cbs[i].cb = cb;
	pktp->cbs[i].arg = arg;
	pktp->cbs[i].refcnt++;
	pktp->cbcnt++;

	/* force enable empty callback */
	pktpool_emptycb_disable(pktp, FALSE);
done:
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return err;
}

/* No BCMATTACHFN as it is used in a non-attach function */
int
pktpool_avail_deregister(pktpool_t *pktp, pktpool_cb_t cb, void *arg)
{
	int err = 0;
	int i, k;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS) {
		return BCME_ERROR;
	}

	ASSERT(cb != NULL);

	for (i = 0; i < pktp->cbcnt; i++) {
		ASSERT(pktp->cbs[i].cb != NULL);
		if ((cb == pktp->cbs[i].cb) && (arg == pktp->cbs[i].arg)) {
			pktp->cbs[i].refcnt--;
			if (pktp->cbs[i].refcnt) {
				/* Still there are references to this callback */
				goto done;
			}
			/* Moving any more callbacks to fill the hole */
			for (k = i+1; k < pktp->cbcnt; i++, k++) {
				pktp->cbs[i].cb = pktp->cbs[k].cb;
				pktp->cbs[i].arg = pktp->cbs[k].arg;
				pktp->cbs[i].refcnt = pktp->cbs[k].refcnt;
			}

			/* reset the last callback */
			pktp->cbs[i].cb = NULL;
			pktp->cbs[i].arg = NULL;
			pktp->cbs[i].refcnt = 0;

			pktp->cbcnt--;
			goto done;
		}
	}

done:
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS) {
		return BCME_ERROR;
	}

	return err;
}

/** Registers callback functions */
int
BCMATTACHFN(pktpool_empty_register)(pktpool_t *pktp, pktpool_cb_t cb, void *arg)
{
	int err = 0;
	int i;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	ASSERT(cb != NULL);

	i = pktp->ecbcnt;
	if (i == PKTPOOL_CB_MAX) {
		err = BCME_ERROR;
		goto done;
	}

	ASSERT(pktp->ecbs[i].cb == NULL);
	pktp->ecbs[i].cb = cb;
	pktp->ecbs[i].arg = arg;
	pktp->ecbcnt++;

done:
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return err;
}

/** Calls registered callback functions */
static int
BCMPOSTTRAPFN(pktpool_empty_notify)(pktpool_t *pktp)
{
	int i;

	pktp->empty = TRUE;
	for (i = 0; i < pktp->ecbcnt; i++) {
		ASSERT(pktp->ecbs[i].cb != NULL);
		pktp->ecbs[i].cb(pktp, pktp->ecbs[i].arg);
	}
	pktp->empty = FALSE;

	return 0;
}

#ifdef BCMDBG_POOL
int
pktpool_dbg_register(pktpool_t *pktp, pktpool_cb_t cb, void *arg)
{
	int err = 0;
	int i;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	ASSERT(cb);

	i = pktp->dbg_cbcnt;
	if (i == PKTPOOL_CB_MAX) {
		err = BCME_ERROR;
		goto done;
	}

	ASSERT(pktp->dbg_cbs[i].cb == NULL);
	pktp->dbg_cbs[i].cb = cb;
	pktp->dbg_cbs[i].arg = arg;
	pktp->dbg_cbcnt++;

done:
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return err;
}

int pktpool_dbg_notify(pktpool_t *pktp);

int
pktpool_dbg_notify(pktpool_t *pktp)
{
	int i;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	for (i = 0; i < pktp->dbg_cbcnt; i++) {
		ASSERT(pktp->dbg_cbs[i].cb);
		pktp->dbg_cbs[i].cb(pktp, pktp->dbg_cbs[i].arg);
	}

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return 0;
}

int
pktpool_dbg_dump(pktpool_t *pktp)
{
	int i;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	printf("pool len=%d maxlen=%d\n",  pktp->dbg_qlen, pktp->maxlen);
	for (i = 0; i < pktp->dbg_qlen; i++) {
		ASSERT(pktp->dbg_q[i].p);
		printf("%d, p: 0x%x dur:%lu us state:%d\n", i,
			pktp->dbg_q[i].p, pktp->dbg_q[i].dur/100, PKTPOOLSTATE(pktp->dbg_q[i].p));
	}

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return 0;
}

int
pktpool_stats_dump(pktpool_t *pktp, pktpool_stats_t *stats)
{
	int i;
	int state;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	bzero(stats, sizeof(pktpool_stats_t));
	for (i = 0; i < pktp->dbg_qlen; i++) {
		ASSERT(pktp->dbg_q[i].p != NULL);

		state = PKTPOOLSTATE(pktp->dbg_q[i].p);
		switch (state) {
			case POOL_TXENQ:
				stats->enq++; break;
			case POOL_TXDH:
				stats->txdh++; break;
			case POOL_TXD11:
				stats->txd11++; break;
			case POOL_RXDH:
				stats->rxdh++; break;
			case POOL_RXD11:
				stats->rxd11++; break;
			case POOL_RXFILL:
				stats->rxfill++; break;
			case POOL_IDLE:
				stats->idle++; break;
		}
	}

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return 0;
}

int
pktpool_start_trigger(pktpool_t *pktp, void *p)
{
	uint32 cycles, i;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	if (!PKTPOOL(OSH_NULL, p))
		goto done;

	OSL_GETCYCLES(cycles);

	for (i = 0; i < pktp->dbg_qlen; i++) {
		ASSERT(pktp->dbg_q[i].p != NULL);

		if (pktp->dbg_q[i].p == p) {
			pktp->dbg_q[i].cycles = cycles;
			break;
		}
	}

done:
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return 0;
}

int pktpool_stop_trigger(pktpool_t *pktp, void *p);

int
pktpool_stop_trigger(pktpool_t *pktp, void *p)
{
	uint32 cycles, i;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	if (!PKTPOOL(OSH_NULL, p))
		goto done;

	OSL_GETCYCLES(cycles);

	for (i = 0; i < pktp->dbg_qlen; i++) {
		ASSERT(pktp->dbg_q[i].p != NULL);

		if (pktp->dbg_q[i].p == p) {
			if (pktp->dbg_q[i].cycles == 0)
				break;

			if (cycles >= pktp->dbg_q[i].cycles)
				pktp->dbg_q[i].dur = cycles - pktp->dbg_q[i].cycles;
			else
				pktp->dbg_q[i].dur =
					(((uint32)-1) - pktp->dbg_q[i].cycles) + cycles + 1;

			pktp->dbg_q[i].cycles = 0;
			break;
		}
	}

done:
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return 0;
}
#endif /* BCMDBG_POOL */

int
pktpool_avail_notify_normal(osl_t *osh, pktpool_t *pktp)
{
	BCM_REFERENCE(osh);
	ASSERT(pktp);

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	pktp->availcb_excl = NULL;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return 0;
}

int
pktpool_avail_notify_exclusive(osl_t *osh, pktpool_t *pktp, pktpool_cb_t cb)
{
	int i;
	int err;
	BCM_REFERENCE(osh);

	ASSERT(pktp);

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	ASSERT(pktp->availcb_excl == NULL);
	for (i = 0; i < pktp->cbcnt; i++) {
		if (cb == pktp->cbs[i].cb) {
			pktp->availcb_excl = &pktp->cbs[i];
			break;
		}
	}

	if (pktp->availcb_excl == NULL)
		err = BCME_ERROR;
	else
		err = 0;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return err;
}

static void
BCMPOSTTRAPFN(pktpool_avail_notify)(pktpool_t *pktp)
{
	int i, k, idx;

	ASSERT(pktp);
	pktpool_emptycb_disable(pktp, TRUE);

	if (pktp->availcb_excl != NULL) {
		pktp->availcb_excl->cb(pktp, pktp->availcb_excl->arg);
		return;
	}

	k = pktp->cbcnt - 1;
	for (i = 0; i < pktp->cbcnt; i++) {
		/* callbacks are getting disabled at this func entry.
		 * For the case of avail is say 5, and first callback
		 * consumes exactly 5 due to dma rxpost setting, then
		 * further callbacks will not getting notified if avail check
		 * is present.
		 * so calling all cbs even if pktp->avail is zero, so that
		 * cbs get oppurtunity to enable callbacks if their
		 * operation is in progress / not completed.
		 */
		if (pktp->cbtoggle)
			idx = i;
		else
			idx = k--;

		ASSERT(pktp->cbs[idx].cb != NULL);
		pktp->cbs[idx].cb(pktp, pktp->cbs[idx].arg);
	}

	/* Alternate between filling from head or tail
	 */
	pktp->cbtoggle ^= 1;

	return;
}

#ifdef APP_RX
/* Update freelist and avail count for a given packet pool */
void
BCMFASTPATH(pktpool_update_freelist)(pktpool_t *pktp, void *p, uint pkts_consumed)
{
	ASSERT_FP(pktp->avail >= pkts_consumed);

	pktp->freelist = p;
	pktp->avail -= pkts_consumed;
}
#endif /* APP_RX */

/** Gets an empty packet from the caller provided pool */
void *
BCMPOSTTRAPFASTPATH(pktpool_get_ext)(pktpool_t *pktp, uint8 type, uint *pktcnt)
{
	void *p = NULL;
	uint pkts_requested = 1;
#if defined(DONGLEBUILD)
	uint pkts_avail;
	bool rxcpl = (pktp->rxcplidfn.cb != NULL) ? TRUE : FALSE;
#endif /* DONGLEBUILD */

	if (pktcnt) {
		pkts_requested = *pktcnt;
		if (pkts_requested == 0) {
			goto done;
		}
	}

#if defined(DONGLEBUILD)
	pkts_avail = pkts_requested;
#endif /* DONGLEBUILD */

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return NULL;

	/* If there are lesser packets in the pool than requested, call
	 * pktpool_empty_notify() to reclaim more pkts.
	 */
	if (pktp->avail < pkts_requested) {
		/* Notify and try to reclaim tx pkts */
		if (pktp->ecbcnt) {
			pktpool_empty_notify(pktp);
		}

		if (pktp->avail < pkts_requested) {
			pktpool_emptycb_disable(pktp, FALSE);
			if (pktp->avail == 0) {
				goto done;
			}
		}
	}

#ifdef APP_RX
	if (pktcnt) {
		p = pktp->freelist;
		if (pktp->avail < pkts_requested) {
			pkts_avail = pktp->avail;
		}

		/* For rx frags in APP, we need to return only the head of freelist and
		 * the caller operates on it and updates the avail count and freelist pointer
		 * using pktpool_update_freelist().
		 */
		if (BCMSPLITRX_ENAB() && ((type == lbuf_rxfrag) || (type == lbuf_rxdata))) {
			*pktcnt = pkts_avail;
			goto done;
		}
	} else
#endif /* APP_RX */
	{
		ASSERT_FP(pkts_requested == 1);
		p = pktpool_deq(pktp);
	}

	ASSERT_FP(p);

#if defined(DONGLEBUILD)
#ifndef APP_RX
	if (BCMSPLITRX_ENAB() && (type == lbuf_rxfrag)) {
		/* If pool is shared rx pool, use call back fn to populate host address.
		 * In case of APP, callback may use lesser number of packets than what
		 * we have given to callback because of some resource crunch and the exact
		 * number of packets that are used by the callback are returned using
		 * (*pktcnt) and the pktpool freelist head is updated accordingly.
		 */
		ASSERT_FP(pktp->cbext.cb != NULL);
		if (pktp->cbext.cb(pktp, pktp->cbext.arg, p, rxcpl, &pkts_avail)) {
			pktpool_enq(pktp, p);
			p = NULL;
		}
	}
#endif /* APP_RX */

	if ((type == lbuf_basic) && rxcpl) {
		/* If pool is shared rx pool, use call back fn to populate Rx cpl ID */
		ASSERT_FP(pktp->rxcplidfn.cb != NULL);
		/* If rxcplblock is allocated */
		if (pktp->rxcplidfn.cb(pktp, pktp->rxcplidfn.arg, p, TRUE, NULL)) {
			pktpool_enq(pktp, p);
			p = NULL;
		}
	}
#endif /* _DONGLEBUILD_ */

done:
	if ((pktp->avail == 0) && (pktp->emptycb_disable == EMPTYCB_SKIPPED)) {
		pktp->emptycb_disable = EMPTYCB_DISABLED;
	}
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return NULL;

	return p;
}

void
BCMFASTPATH(pktpool_nfree)(pktpool_t *pktp, void *head, void *tail, uint count)
{
#ifdef BCMRXDATAPOOL
	void *_head = head;
#endif /* BCMRXDATAPOOL */

	if (count > 1) {
		pktp->avail += (count - 1);

#ifdef BCMRXDATAPOOL
		while (--count) {
			_head = PKTLINK(_head);
			ASSERT_FP(_head);
			pktpool_enq(pktpool_shared_rxdata, PKTDATA(OSH_NULL, _head));
		}
#endif /* BCMRXDATAPOOL */

		PKTSETFREELIST(tail, pktp->freelist);
		pktp->freelist = PKTLINK(head);
		PKTSETLINK(head, NULL);
	}
	pktpool_free(pktp, head);
}

void
BCMPOSTTRAPFASTPATH(pktpool_free)(pktpool_t *pktp, void *p)
{
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return;

	ASSERT_FP(p != NULL);
#ifdef BCMDBG_POOL
	/* pktpool_stop_trigger(pktp, p); */
#endif

#ifdef BCMRXDATAPOOL
	/* Free rx data buffer to rx data buffer pool */
	if (PKT_IS_RX_PKT(OSH_NULL, p)) {
		pktpool_t *_pktp = pktpool_shared_rxdata;
		if (PKTISRXFRAG(OSH_NULL, p)) {
			_pktp->cbext.cb(_pktp, _pktp->cbext.arg, p, REMOVE_RXCPLID, NULL);
			PKTRESETRXFRAG(OSH_NULL, p);
		}
		pktpool_enq(pktpool_shared_rxdata, PKTDATA(OSH_NULL, p));
	}
#endif /* BCMRXDATAPOOL */

	pktpool_enq(pktp, p);

	/**
	 * Feed critical DMA with freshly freed packets, to avoid DMA starvation.
	 * If any avail callback functions are registered, send a notification
	 * that a new packet is available in the pool.
	 */
	if (pktp->cbcnt) {
		/* To more efficiently use the cpu cycles, callbacks can be temporarily disabled.
		 * This allows to feed on burst basis as opposed to inefficient per-packet basis.
		 */
		if (pktp->emptycb_disable == EMPTYCB_ENABLED) {
			/**
			 * If the call originated from pktpool_empty_notify, the just freed packet
			 * is needed in pktpool_get.
			 * Therefore don't call pktpool_avail_notify.
			 */
			if (pktp->empty == FALSE)
				pktpool_avail_notify(pktp);
		} else {
			/**
			 * The callback is temporarily disabled, log that a packet has been freed.
			 */
			pktp->emptycb_disable = EMPTYCB_SKIPPED;
		}
	}

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return;
}

/** Adds a caller provided (empty) packet to the caller provided pool */
int
pktpool_add(pktpool_t *pktp, void *p)
{
	int err = 0;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	ASSERT(p != NULL);

	if (pktp->n_pkts == pktp->maxlen) {
		err = BCME_RANGE;
		goto done;
	}

	/* pkts in pool have same length */
	ASSERT(pktp->max_pkt_bytes == PKTLEN(OSH_NULL, p));
	PKTSETPOOL(OSH_NULL, p, TRUE, pktp);

	pktp->n_pkts++;
	pktpool_enq(pktp, p);

#ifdef BCMDBG_POOL
	pktp->dbg_q[pktp->dbg_qlen++].p = p;
#endif

done:
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return err;
}

/**
 * Force pktpool_setmaxlen () into RAM as it uses a constant
 * (PKTPOOL_LEN_MAX) that may be changed post tapeout for ROM-based chips.
 */
int
BCMRAMFN(pktpool_setmaxlen)(pktpool_t *pktp, uint16 maxlen)
{
	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_ACQUIRE(&pktp->mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	if (maxlen > PKTPOOL_LEN_MAX)
		maxlen = PKTPOOL_LEN_MAX;

	/* if pool is already beyond maxlen, then just cap it
	 * since we currently do not reduce the pool len
	 * already allocated
	 */
	pktp->maxlen = (pktp->n_pkts > maxlen) ? pktp->n_pkts : maxlen;

	/* protect shared resource */
	if (HND_PKTPOOL_MUTEX_RELEASE(&pktp->mutex) != OSL_EXT_SUCCESS)
		return BCME_ERROR;

	return pktp->maxlen;
}

void
BCMPOSTTRAPFN(pktpool_emptycb_disable)(pktpool_t *pktp, bool disable)
{
	bool notify = FALSE;
	ASSERT(pktp);

	/**
	 * To more efficiently use the cpu cycles, callbacks can be temporarily disabled.
	 * If callback is going to be re-enabled, check if any packet got
	 * freed and added back to the pool while callback was disabled.
	 * When this is the case do the callback now, provided that callback functions
	 * are registered and this call did not originate from pktpool_empty_notify.
	 */
	if ((!disable) && (pktp->cbcnt) && (pktp->empty == FALSE) &&
		(pktp->emptycb_disable == EMPTYCB_SKIPPED)) {
		notify = TRUE;
	}

	/* Enable or temporarily disable callback when packet becomes available. */
	if (disable) {
		if (pktp->emptycb_disable == EMPTYCB_ENABLED) {
			/* mark disabled only if enabled.
			 * if state is EMPTYCB_SKIPPED, it means already
			 * disabled and some pkts are freed. So don't lose the state
			 * of skipped to ensure calling pktpool_avail_notify().
			 */
			pktp->emptycb_disable = EMPTYCB_DISABLED;
		}
	} else {
		pktp->emptycb_disable = EMPTYCB_ENABLED;
	}
	if (notify) {
		/* pktpool_emptycb_disable() is called from pktpool_avail_notify() and
		 * pktp->cbs. To have the result of most recent call, notify after
		 * emptycb_disable is modified.
		 * This change also prevents any recursive calls of pktpool_avail_notify()
		 * from pktp->cbs if pktpool_emptycb_disable() is called from them.
		 */
		pktpool_avail_notify(pktp);
	}
}

bool
pktpool_emptycb_disabled(pktpool_t *pktp)
{
	ASSERT(pktp);
	return pktp->emptycb_disable != EMPTYCB_ENABLED;
}

#ifdef BCMPKTPOOL
#include <hnd_lbuf.h>

pktpool_t *pktpool_shared = NULL;

#ifdef BCMFRAGPOOL
pktpool_t *pktpool_shared_lfrag = NULL;
#ifdef BCMRESVFRAGPOOL
pktpool_t *pktpool_resv_lfrag = NULL;
struct resv_info *resv_pool_info = NULL;
#endif /* BCMRESVFRAGPOOL */
#endif /* BCMFRAGPOOL */

#ifdef BCMALFRAGPOOL
pktpool_t *pktpool_shared_alfrag = NULL;
pktpool_t *pktpool_shared_alfrag_data = NULL;
#endif /* BCMCTFRAGPOOL */

pktpool_t *pktpool_shared_rxlfrag = NULL;

/* Rx data pool w/o rxfrag structure */
pktpool_t *pktpool_shared_rxdata = NULL;

static osl_t *pktpool_osh = NULL;

/**
 * Initializes several packet pools and allocates packets within those pools.
 */
int
BCMATTACHFN(hnd_pktpool_init)(osl_t *osh)
{
	int err = BCME_OK;
	int n, pktsz;
	bool is_heap_pool;

	BCM_REFERENCE(pktsz);
	BCM_REFERENCE(is_heap_pool);

	/* Construct a packet pool registry before initializing packet pools */
	n = pktpool_attach(osh, PKTPOOL_MAXIMUM_ID);
	if (n != PKTPOOL_MAXIMUM_ID) {
		ASSERT(0);
		err = BCME_ERROR;
		goto error;
	}

	pktpool_shared = MALLOCZ(osh, sizeof(pktpool_t));
	if (pktpool_shared == NULL) {
		ASSERT(0);
		err = BCME_NOMEM;
		goto error;
	}

#if defined(BCMFRAGPOOL) && !defined(BCMFRAGPOOL_DISABLED)
	pktpool_shared_lfrag = MALLOCZ(osh, sizeof(pktpool_t));
	if (pktpool_shared_lfrag == NULL) {
		ASSERT(0);
		err = BCME_NOMEM;
		goto error;
	}

#if defined(BCMRESVFRAGPOOL) && !defined(BCMRESVFRAGPOOL_DISABLED)
	resv_pool_info = hnd_resv_pool_alloc(osh);
	if (resv_pool_info == NULL) {
		err = BCME_NOMEM;
		ASSERT(0);
		goto error;
	}
	pktpool_resv_lfrag = resv_pool_info->pktp;
	if (pktpool_resv_lfrag == NULL) {
		err = BCME_ERROR;
		ASSERT(0);
		goto error;
	}
#endif	/* RESVFRAGPOOL */
#endif /* FRAGPOOL */

#if defined(BCMALFRAGPOOL) && !defined(BCMALFRAGPOOL_DISABLED)
	pktpool_shared_alfrag = MALLOCZ(osh, sizeof(pktpool_t));
	if (pktpool_shared_alfrag == NULL) {
		ASSERT(0);
		err = BCME_NOMEM;
		goto error;
	}

	pktpool_shared_alfrag_data = MALLOCZ(osh, sizeof(pktpool_t));
	if (pktpool_shared_alfrag_data == NULL) {
		ASSERT(0);
		err = BCME_NOMEM;
		goto error;
	}
#endif /* BCMCTFRAGPOOL */

#if defined(BCMRXFRAGPOOL) && !defined(BCMRXFRAGPOOL_DISABLED)
	pktpool_shared_rxlfrag = MALLOCZ(osh, sizeof(pktpool_t));
	if (pktpool_shared_rxlfrag == NULL) {
		ASSERT(0);
		err = BCME_NOMEM;
		goto error;
	}
#endif

#if defined(BCMRXDATAPOOL) && !defined(BCMRXDATAPOOL_DISABLE)
	pktpool_shared_rxdata = MALLOCZ(osh, sizeof(pktpool_t));
	if (pktpool_shared_rxdata == NULL) {
		ASSERT(0);
		err = BCME_NOMEM;
		goto error;
	}
#endif

	/*
	 * At this early stage, there's not enough memory to allocate all
	 * requested pkts in the shared pool.  Need to add to the pool
	 * after reclaim
	 *
	 * n = NRXBUFPOST + SDPCMD_RXBUFS;
	 *
	 * Initialization of packet pools may fail (BCME_ERROR), if the packet pool
	 * registry is not initialized or the registry is depleted.
	 *
	 * A BCME_NOMEM error only indicates that the requested number of packets
	 * were not filled into the pool.
	 */
	n = 1;
	MALLOC_SET_NOPERSIST(osh); /* Ensure subsequent allocations are non-persist */
	if ((err = pktpool_init(osh, pktpool_shared,
			&n, PKTBUFSZ, FALSE, lbuf_basic, FALSE, 0, 0)) != BCME_OK) {
		ASSERT(0);
		goto error;
	}
	pktpool_setmaxlen(pktpool_shared, SHARED_POOL_LEN);

#if defined(BCMFRAGPOOL) && !defined(BCMFRAGPOOL_DISABLED)
	n = 1;
#if (((defined(EVENTLOG_D3_PRESERVE) && !defined(EVENTLOG_D3_PRESERVE_DISABLED)) || \
		defined(BCMPOOLRECLAIM)))
	is_heap_pool = TRUE;
#else
	is_heap_pool = FALSE;
#endif /* (( EVENTLOG_D3_PRESERVE && !EVENTLOG_D3_PRESERVE_DISABLED) || BCMPOOLRECLAIM)  */

	if ((err = pktpool_init(osh, pktpool_shared_lfrag, &n, PKTFRAGSZ, TRUE, lbuf_frag,
			is_heap_pool, POOL_HEAP_FLAG_D3, SHARED_FRAG_POOL_LEN >> 3)) !=
			BCME_OK) {
		ASSERT(0);
		goto error;
	}
	pktpool_setmaxlen(pktpool_shared_lfrag, SHARED_FRAG_POOL_LEN);

#if defined(BCMRESVFRAGPOOL) && !defined(BCMRESVFRAGPOOL_DISABLED)
	n = 0; /* IMPORTANT: DO NOT allocate any packets in resv pool */
#ifdef RESV_POOL_HEAP
	is_heap_pool = TRUE;
#else
	is_heap_pool = FALSE;
#endif /* RESV_POOL_HEAP */

	if ((err = pktpool_init(osh, pktpool_resv_lfrag, &n, PKTFRAGSZ, TRUE, lbuf_frag,
		is_heap_pool, POOL_HEAP_FLAG_RSRVPOOL, 0)) != BCME_OK) {
		ASSERT(0);
		goto error;
	}
	pktpool_setmaxlen(pktpool_resv_lfrag, RESV_FRAG_POOL_LEN);
#endif /* RESVFRAGPOOL */
#endif /* BCMFRAGPOOL */

#if defined(BCMALFRAGPOOL) && !defined(BCMALFRAGPOOL_DISABLED)
	n = 1;
	is_heap_pool = FALSE;

	if ((err = pktpool_init(osh, pktpool_shared_alfrag, &n, PKTFRAGSZ, TRUE, lbuf_alfrag,
			is_heap_pool, 0, SHARED_ALFRAG_POOL_LEN >> 3)) != BCME_OK) {
		ASSERT(0);
		goto error;
	}
	pktpool_setmaxlen(pktpool_shared_alfrag, SHARED_ALFRAG_POOL_LEN);

	n = 0;
	if ((err = pktpool_init(osh, pktpool_shared_alfrag_data, &n, TXPKTALFRAG_DATA_BUFSZ, TRUE,
			lbuf_alfrag_data, FALSE, 0, SHARED_ALFRAG_DATA_POOL_LEN >> 3)) != BCME_OK) {
		ASSERT(0);
		goto error;
	}
	pktpool_setmaxlen(pktpool_shared_alfrag_data, SHARED_ALFRAG_DATA_POOL_LEN);

#endif /* BCMCTFRAGPOOL */

#if defined(BCMRXFRAGPOOL) && !defined(BCMRXFRAGPOOL_DISABLED)
#if defined(BCMRXDATAPOOL) && !defined(BCMRXDATAPOOL_DISABLED)
	n = 1;
	if ((err = pktpool_init(osh, pktpool_shared_rxdata, &n, RXPKTFRAGDATASZ, TRUE, lbuf_rxdata,
			FALSE, 0, 0)) != BCME_OK) {
		ASSERT(0);
		goto error;
	}
	pktpool_setmaxlen(pktpool_shared_rxdata, SHARED_RXDATA_POOL_LEN);

	pktsz = 0;
#else
	pktsz = RXPKTFRAGDATASZ;
#endif /* defined(BCMRXDATAPOOL) && !defined(BCMRXDATAPOOL_DISABLED) */

#ifdef RESV_POOL_HEAP
	is_heap_pool = BCMPOOLRECLAIM_ENAB() ? TRUE : FALSE;
#else
	is_heap_pool = FALSE;
#endif /* RESV_POOL_HEAP */

	n = 1;
	if ((err = pktpool_init(osh, pktpool_shared_rxlfrag, &n, pktsz, TRUE, lbuf_rxfrag,
			is_heap_pool, POOL_HEAP_FLAG_D3, 0)) != BCME_OK) {
		ASSERT(0);
		goto error;
	}

	pktpool_setmaxlen(pktpool_shared_rxlfrag, SHARED_RXFRAG_POOL_LEN);
#endif /* defined(BCMRXFRAGPOOL) && !defined(BCMRXFRAGPOOL_DISABLED) */

#if defined(BCMFRWDPOOLREORG) && !defined(BCMFRWDPOOLREORG_DISABLED)
	/* Attach poolreorg module */
	if ((frwd_poolreorg_info = poolreorg_attach(osh,
#if defined(BCMFRAGPOOL) && !defined(BCMFRAGPOOL_DISABLED)
			pktpool_shared_lfrag,
#else
			NULL,
#endif /* defined(BCMFRAGPOOL) && !defined(BCMFRAGPOOL_DISABLED) */
#if defined(BCMRXFRAGPOOL) && !defined(BCMRXFRAGPOOL_DISABLED)
			pktpool_shared_rxlfrag,
#else
			NULL,
#endif /* BCMRXFRAGPOOL */
			pktpool_shared)) == NULL) {
		ASSERT(0);
		err = BCME_NOMEM;
		goto error;
	}
#endif /* defined(BCMFRWDPOOLREORG) && !defined(BCMFRWDPOOLREORG_DISABLED) */

	pktpool_osh = osh;
	MALLOC_CLEAR_NOPERSIST(osh);

#ifdef POOL_HEAP_RECONFIG
	lbuf_free_cb_set(hnd_pktpool_lbuf_free_cb);
#endif

	return BCME_OK;

error:
	hnd_pktpool_deinit(osh);

	return err;
} /* hnd_pktpool_init */

void
BCMATTACHFN(hnd_pktpool_deinit)(osl_t *osh)
{
#if defined(BCMFRWDPOOLREORG) && !defined(BCMFRWDPOOLREORG_DISABLED)
	if (frwd_poolreorg_info != NULL) {
		poolreorg_detach(frwd_poolreorg_info);
	}
#endif /* defined(BCMFRWDPOOLREORG) && !defined(BCMFRWDPOOLREORG_DISABLED) */

#if defined(BCMRXFRAGPOOL) && !defined(BCMRXFRAGPOOL_DISABLED)
	if (pktpool_shared_rxlfrag != NULL) {
		if (pktpool_shared_rxlfrag->inited) {
			pktpool_deinit(osh, pktpool_shared_rxlfrag);
		}

		hnd_free(pktpool_shared_rxlfrag);
		pktpool_shared_rxlfrag = (pktpool_t *)NULL;
	}
#endif

#if defined(BCMRXDATAPOOL) && !defined(BCMRXDATAPOOL_DISABLED)
	if (pktpool_shared_rxdata != NULL) {
		if (pktpool_shared_rxdata->inited) {
			pktpool_deinit(osh, pktpool_shared_rxdata);
		}

		hnd_free(pktpool_shared_rxdata);
		pktpool_shared_rxdata = (pktpool_t *)NULL;
	}
#endif

#if defined(BCMFRAGPOOL) && !defined(BCMFRAGPOOL_DISABLED)
	if (pktpool_shared_lfrag != NULL) {
		if (pktpool_shared_lfrag->inited) {
			pktpool_deinit(osh, pktpool_shared_lfrag);
		}
		hnd_free(pktpool_shared_lfrag);
		pktpool_shared_lfrag = (pktpool_t *)NULL;
	}
#endif /* BCMFRAGPOOL */

#if defined(BCMALFRAGPOOL) && !defined(BCMALFRAGPOOL_DISABLED)
	if (pktpool_shared_alfrag != NULL) {
		if (pktpool_shared_alfrag->inited) {
			pktpool_deinit(osh, pktpool_shared_alfrag);
		}
		hnd_free(pktpool_shared_alfrag);
		pktpool_shared_alfrag = (pktpool_t *)NULL;
	}

	if (pktpool_shared_alfrag_data != NULL) {
		if (pktpool_shared_alfrag_data->inited) {
			pktpool_deinit(osh, pktpool_shared_alfrag_data);
		}

		hnd_free(pktpool_shared_alfrag_data);
		pktpool_shared_alfrag_data = (pktpool_t *)NULL;
	}
#endif /* BCMFRAGPOOL */

#if defined(BCMRESVFRAGPOOL) && !defined(BCMRESVFRAGPOOL_DISABLED)
	if (resv_pool_info != NULL) {
		if (pktpool_resv_lfrag != NULL) {
			pktpool_resv_lfrag = NULL;
		}
		hnd_free(resv_pool_info);
	}
#endif /* RESVFRAGPOOL */

	if (pktpool_shared != NULL) {
		if (pktpool_shared->inited) {
			pktpool_deinit(osh, pktpool_shared);
		}

		hnd_free(pktpool_shared);
		pktpool_shared = (pktpool_t *)NULL;
	}

	pktpool_dettach(osh);

	MALLOC_CLEAR_NOPERSIST(osh);
}

/** is called at each 'wl up' */
int
hnd_pktpool_fill(pktpool_t *pktpool, bool minimal)
{
	return (pktpool_fill(pktpool_osh, pktpool, minimal));
}

/** refills pktpools after reclaim, is called once */
void
hnd_pktpool_refill(bool minimal)
{
	if (POOL_ENAB(pktpool_shared)) {
#if defined(SRMEM)
		if (SRMEM_ENAB()) {
			int maxlen = pktpool_max_pkts(pktpool_shared);
			int n_pkts = pktpool_tot_pkts(pktpool_shared);

			for (; n_pkts < maxlen; n_pkts++) {
				void *p;
				if ((p = PKTSRGET(pktpool_max_pkt_bytes(pktpool_shared))) == NULL)
					break;
				pktpool_add(pktpool_shared, p);
			}
		}
#endif /* SRMEM */
		pktpool_fill(pktpool_osh, pktpool_shared, minimal);
	}
/* fragpool reclaim */
#ifdef BCMFRAGPOOL
	if (POOL_ENAB(pktpool_shared_lfrag)) {
		pktpool_fill(pktpool_osh, pktpool_shared_lfrag, minimal);
	}
#endif /* BCMFRAGPOOL */

/* alfragpool reclaim */
#ifdef BCMALFRAGPOOL
	if (POOL_ENAB(pktpool_shared_alfrag)) {
		pktpool_fill(pktpool_osh, pktpool_shared_alfrag, minimal);
	}

	if (POOL_ENAB(pktpool_shared_alfrag_data)) {
		pktpool_fill(pktpool_osh, pktpool_shared_alfrag_data, minimal);
	}
#endif /* BCMALFRAGPOOL */

/* rx fragpool reclaim */
#ifdef BCMRXFRAGPOOL
	if (POOL_ENAB(pktpool_shared_rxlfrag)) {
		pktpool_fill(pktpool_osh, pktpool_shared_rxlfrag, minimal);
	}
#endif

#ifdef BCMRXDATAPOOL
	if (POOL_ENAB(pktpool_shared_rxdata)) {
		pktpool_fill(pktpool_osh, pktpool_shared_rxdata, minimal);
	}
#endif

#if defined(BCMFRAGPOOL) && defined(BCMRESVFRAGPOOL)
	if (POOL_ENAB(pktpool_resv_lfrag)) {
		int resv_size = (pktpool_resv_lfrag->max_pkt_bytes + LBUFFRAGSZ) *
			pktpool_resv_lfrag->maxlen;
		hnd_resv_pool_init(resv_pool_info, resv_size);
		hnd_resv_pool_enable(resv_pool_info);
	}
#endif /* BCMRESVFRAGPOOL */
}

#ifdef POOL_HEAP_RECONFIG
#define hnd_pktpool_release_active_set(pktp)	(pktpool_heap_rel_active |= (1 << pktp->id))
#define hnd_pktpool_release_active_reset(pktp)	(pktpool_heap_rel_active &= ~(1 << pktp->id))
/* Function enable/disable heap pool usage */

void
hnd_pktpool_heap_handle(osl_t *osh, uint32 flag, bool enable)
{
	int i = 0;
	pktpool_t *pktp;
	/*
	 * Loop through all the registerd pktpools.
	 * Trigger retreave of pkts from the heap back to pool if no
	 * flags are active.
	 */
	for (i = 1; i < PKTPOOL_MAXIMUM_ID; i++) {
		if ((pktp = get_pktpools_registry(i)) != NULL) {
			if ((flag == pktp->poolheap_flag) && pktp->is_heap_pool) {
				if (enable) {
					hnd_pktpool_heap_pkt_release(pktpool_osh, pktp, flag);
				} else {
					hnd_pktpool_heap_pkt_retrieve(pktp, flag);
				}
			}
		}
	}
}

/* Do memory allocation from pool heap memory */
void *
hnd_pktpool_freelist_alloc(uint size, uint alignbits, uint32 flag)
{
	int i = 0;
	pktpool_t *pktp;
	void *p = NULL;
	for (i = 1; i < PKTPOOL_MAXIMUM_ID; i++) {
		if ((pktp = get_pktpools_registry(i)) != NULL) {
			if ((flag == pktp->poolheap_flag) && pktp->is_heap_pool) {
				p = rte_freelist_mgr_alloc(size, alignbits, pktp->mem_handle);
				if (p)
					break;
			}
		}
	}
	return p;
}

/* Release pkts from pool to free heap */
static void
hnd_pktpool_heap_pkt_release(osl_t *osh, pktpool_t *pktp, uint32 flag)
{
	pktpool_cb_extn_t cb = NULL;
	void *arg = NULL;
	int i = 0;
	pktpool_heap_cb_reg_t *pktp_heap_cb = hnd_pool_get_cb_registry();

	pktp->release_active = FALSE;
	hnd_pktpool_release_active_reset(pktp);

	if (pktp->n_pkts <= pktp->min_backup_buf)
		return;
	/* call module specific callbacks */
	if (BCMSPLITRX_ENAB() && (pktp->type == lbuf_rxfrag)) {
		/* If pool is shared rx frag pool, use call back fn to reclaim host address
		 * and Rx cpl ID associated with the pkt.
		 */
		ASSERT(pktp->cbext.cb != NULL);
		cb = pktp->cbext.cb;
		arg = pktp->cbext.arg;
	} else if ((pktp->type == lbuf_basic) && (pktp->rxcplidfn.cb != NULL)) {
		/* If pool is shared rx pool, use call back fn to freeup Rx cpl ID
		 * associated with the pkt.
		 */
		cb = pktp->rxcplidfn.cb;
		arg = pktp->rxcplidfn.arg;
	}

	while (pktp->avail > pktp->min_backup_buf) {
		void * p = pktp->freelist;

		pktp->freelist = PKTFREELIST(p); /* unlink head packet from free list */
		PKTSETFREELIST(p, NULL);

		if (cb != NULL) {
			if (cb(pktp, arg, p, REMOVE_RXCPLID, NULL)) {
				PKTSETFREELIST(p, pktp->freelist);
				pktp->freelist = p;
				break;
			}
		}

		PKTSETPOOL(osh, p, FALSE, NULL); /* clear pool ID tag in pkt */

		lb_set_nofree(p);
		total_pool_pktid_count++;
		PKTFREE(osh, p, pktp->istx); /* free the packet */

		rte_freelist_mgr_add(p, pktp->mem_handle);
		pktp->avail--;
		pktp->n_pkts--;
		pktp->poolheap_count++;
	}

	/* Execute call back for upper layer which used pkt from heap */
	for (i = 0; i < PKTPOOL_MAX_HEAP_CB; i++) {
		if ((pktp_heap_cb[i].fn != NULL) &&
			(flag == pktp_heap_cb[i].flag))
			(pktp_heap_cb[i].fn)(pktp_heap_cb[i].ctxt, TRUE);
	}

}

static pktpool_heap_cb_reg_t *
BCMRAMFN(hnd_pool_get_cb_registry)(void)
{
	return pktpool_heap_cb_reg;
}

static void
BCMFASTPATH(hnd_pktpool_lbuf_free_cb)(uint8 poolid)
{
	int i = 0;
	pktpool_t *pktp;

	if (poolid == PKTPOOL_INVALID_ID && pktpool_heap_rel_active) {
		for (i = 1; i < PKTPOOL_MAXIMUM_ID; i++) {
			if ((pktp = get_pktpools_registry(i)) != NULL) {
				if (pktp->is_heap_pool && (pktp->release_active)) {
					rte_freelist_mgr_release(pktp->mem_handle);
				}
			}
		}
	}
}

/* Take back pkts from free mem and refill pool */
static void
hnd_pktpool_heap_pkt_retrieve(pktpool_t *pktp, uint32 flag)
{
	int i = 0;
	pktpool_heap_cb_reg_t *pktp_heap_cb = hnd_pool_get_cb_registry();
	pktp->release_active = TRUE;
	hnd_pktpool_release_active_set(pktp);

	/* Execute call back for upper layer which used pkt from heap */
	for (i = 0; i < PKTPOOL_MAX_HEAP_CB; i++) {
		if ((pktp_heap_cb[i].fn != NULL) &&
			(flag == pktp_heap_cb[i].flag))
			(pktp_heap_cb[i].fn)(pktp_heap_cb[i].ctxt, FALSE);
	}

	rte_freelist_mgr_release(pktp->mem_handle);
}

/* Function to add back the pkt to pktpool */
static int
hnd_pktpool_heap_get_cb(uint8 handle, void *ctxt, void *pkt, uint pktsize)
{
	pktpool_t *pktp = (pktpool_t *)ctxt;
	struct lbuf *lb;
	int ret = BCME_ERROR;
	if (pktp != NULL) {
		if ((lb = PKTALLOC_ON_LOC(pktpool_osh, pktp->max_pkt_bytes,
			pktp->type, pkt, pktsize)) != NULL) {
			if ((ret = pktpool_add(pktp, lb)) == BCME_OK) {
				pktp->poolheap_count--;
				ASSERT(total_pool_pktid_count > 0);
				total_pool_pktid_count--;
				if (pktp->poolheap_count == 0) {
					pktp->release_active = FALSE;
					hnd_pktpool_release_active_reset(pktp);
				}
				if (pktp->cbcnt) {
					if (pktp->empty == FALSE)
						pktpool_avail_notify(pktp);
				}
			} else {
				/*
				 * pktpool_add failed indicate already max
				 * number of pkts are available in pool. So
				 * free this buffer to heap
				 */
				PKTFREE(pktpool_osh, lb, pktsize);
			}
			ret = BCME_OK;
		}
	}
	return ret;
}

int
hnd_pktpool_heap_register_cb(pktpool_heap_cb_t fn, void *ctxt, uint32 flag)
{
	int i = 0;
	int err = BCME_ERROR;
	pktpool_heap_cb_reg_t *pktp_heap_cb = hnd_pool_get_cb_registry();

	/* Search for free entry */
	for (i = 0; i < PKTPOOL_MAX_HEAP_CB; i++) {
		if (pktp_heap_cb[i].fn == NULL)
			break;
	}

	if (i < PKTPOOL_MAX_HEAP_CB) {
		pktp_heap_cb[i].fn = fn;
		pktp_heap_cb[i].ctxt = ctxt;
		pktp_heap_cb[i].flag = flag;
		err = BCME_OK;
	}
	return err;
}

int
hnd_pktpool_heap_deregister_cb(pktpool_heap_cb_t fn)
{
	int i = 0;
	int err = BCME_ERROR;
	pktpool_heap_cb_reg_t *pktp_heap_cb = hnd_pool_get_cb_registry();

	/* Search for matching entry */
	for (i = 0; i < PKTPOOL_MAX_HEAP_CB; i++) {
		if (pktp_heap_cb[i].fn == fn)
			break;
	}

	if (i < PKTPOOL_MAX_HEAP_CB) {
		pktp_heap_cb[i].fn = NULL;
		err = BCME_OK;
	}
	return err;
}

uint16
hnd_pktpool_get_min_bkup_buf(pktpool_t *pktp)
{
	return pktp->min_backup_buf;
}
#endif /* POOL_HEAP_RECONFIG */

uint32
hnd_pktpool_get_total_poolheap_count(void)
{
	return total_pool_pktid_count;
}
#endif /* BCMPKTPOOL */
