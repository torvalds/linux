/*
 * @file Broadcom Dongle Host Driver (DHD), Flow ring specific code at top level
 *
 * Flow rings are transmit traffic (=propagating towards antenna) related entities
 *
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
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>

#include <ethernet.h>
#include <bcmevent.h>
#include <dngl_stats.h>

#include <dhd.h>

#include <dhd_flowring.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <802.1d.h>
#include <pcie_core.h>
#include <bcmmsgbuf.h>
#include <dhd_pcie.h>
#include <dhd_config.h>

static INLINE int dhd_flow_queue_throttle(flow_queue_t *queue);

static INLINE uint16 dhd_flowid_find(dhd_pub_t *dhdp, uint8 ifindex,
                                     uint8 prio, char *sa, char *da);

static INLINE uint16 dhd_flowid_alloc(dhd_pub_t *dhdp, uint8 ifindex,
                                      uint8 prio, char *sa, char *da);

static INLINE int dhd_flowid_lookup(dhd_pub_t *dhdp, uint8 ifindex,
                                uint8 prio, char *sa, char *da, uint16 *flowid);
int dhd_flow_queue_overflow(flow_queue_t *queue, void *pkt);

#define FLOW_QUEUE_PKT_NEXT(p)          PKTLINK(p)
#define FLOW_QUEUE_PKT_SETNEXT(p, x)    PKTSETLINK((p), (x))

#ifdef DHD_REPLACE_LOG_INFO_TO_TRACE
#define DHD_FLOWRING_INFO DHD_TRACE
#else
#define DHD_FLOWRING_INFO DHD_INFO
#endif /* DHD_REPLACE_LOG_INFO_TO_TRACE */

const uint8 prio2ac[8] = { 0, 1, 1, 0, 2, 2, 3, 3 };
const uint8 prio2tid[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

/** Queue overflow throttle. Return value: TRUE if throttle needs to be applied */
static INLINE int
dhd_flow_queue_throttle(flow_queue_t *queue)
{
#if defined(BCM_ROUTER_DHD)
	/* Two tests
	 * 1) Test whether overall level 2 (grandparent) cummulative threshold crossed.
	 * 2) Or test whether queue's budget and overall cummulative threshold crossed.
	 */
	void *gp_clen_ptr = DHD_FLOW_QUEUE_L2CLEN_PTR(queue);
	void *parent_clen_ptr = DHD_FLOW_QUEUE_CLEN_PTR(queue);
	int gp_cumm_threshold = DHD_FLOW_QUEUE_L2THRESHOLD(queue);
	int cumm_threshold = DHD_FLOW_QUEUE_THRESHOLD(queue);

	int ret = ((DHD_CUMM_CTR_READ(gp_clen_ptr) > gp_cumm_threshold) ||
		((DHD_FLOW_QUEUE_OVFL(queue, DHD_FLOW_QUEUE_MAX(queue))) &&
		(DHD_CUMM_CTR_READ(parent_clen_ptr) > cumm_threshold)));
	return ret;
#else
	return DHD_FLOW_QUEUE_FULL(queue);
#endif /* ! BCM_ROUTER_DHD */
}

int
BCMFASTPATH(dhd_flow_queue_overflow)(flow_queue_t *queue, void *pkt)
{
	return BCME_NORESOURCE;
}

/** Returns flow ring given a flowid */
flow_ring_node_t *
dhd_flow_ring_node(dhd_pub_t *dhdp, uint16 flowid)
{
	flow_ring_node_t * flow_ring_node;

	ASSERT(dhdp != (dhd_pub_t*)NULL);
	ASSERT(flowid <= dhdp->max_tx_flowid);
	if (flowid > dhdp->max_tx_flowid) {
		return NULL;
	}

	flow_ring_node = &(((flow_ring_node_t*)(dhdp->flow_ring_table))[flowid]);

	ASSERT(flow_ring_node->flowid == flowid);
	return flow_ring_node;
}

/** Returns 'backup' queue given a flowid */
flow_queue_t *
dhd_flow_queue(dhd_pub_t *dhdp, uint16 flowid)
{
	flow_ring_node_t * flow_ring_node = NULL;

	flow_ring_node = dhd_flow_ring_node(dhdp, flowid);
	if (flow_ring_node)
		return &flow_ring_node->queue;
	else
		return NULL;
}

/* Flow ring's queue management functions */

/** Reinitialize a flow ring's queue. */
void
dhd_flow_queue_reinit(dhd_pub_t *dhdp, flow_queue_t *queue, int max)
{
	ASSERT((queue != NULL) && (max > 0));

	queue->head = queue->tail = NULL;
	queue->len = 0;

	/* Set queue's threshold and queue's parent cummulative length counter */
	ASSERT(max > 1);
	DHD_FLOW_QUEUE_SET_MAX(queue, max);
	DHD_FLOW_QUEUE_SET_THRESHOLD(queue, max);
	DHD_FLOW_QUEUE_SET_CLEN(queue, &dhdp->cumm_ctr);
	DHD_FLOW_QUEUE_SET_L2CLEN(queue, &dhdp->l2cumm_ctr);

	queue->failures = 0U;
	queue->cb = &dhd_flow_queue_overflow;
}

/** Initialize a flow ring's queue, called on driver initialization. */
void
dhd_flow_queue_init(dhd_pub_t *dhdp, flow_queue_t *queue, int max)
{
	ASSERT((queue != NULL) && (max > 0));

	dll_init(&queue->list);
	dhd_flow_queue_reinit(dhdp, queue, max);
}

/** Register an enqueue overflow callback handler */
void
dhd_flow_queue_register(flow_queue_t *queue, flow_queue_cb_t cb)
{
	ASSERT(queue != NULL);
	queue->cb = cb;
}

/**
 * Enqueue an 802.3 packet at the back of a flow ring's queue. From there, it will travel later on
 * to the flow ring itself.
 */
int
BCMFASTPATH(dhd_flow_queue_enqueue)(dhd_pub_t *dhdp, flow_queue_t *queue, void *pkt)
{
	int ret = BCME_OK;

	ASSERT(queue != NULL);

	if (dhd_flow_queue_throttle(queue)) {
		queue->failures++;
		ret = (*queue->cb)(queue, pkt);
		goto done;
	}

	if (queue->head) {
		FLOW_QUEUE_PKT_SETNEXT(queue->tail, pkt);
	} else {
		queue->head = pkt;
	}

	FLOW_QUEUE_PKT_SETNEXT(pkt, NULL);

	queue->tail = pkt; /* at tail */

	queue->len++;
	/* increment parent's cummulative length */
	DHD_CUMM_CTR_INCR(DHD_FLOW_QUEUE_CLEN_PTR(queue));
	/* increment grandparent's cummulative length */
	DHD_CUMM_CTR_INCR(DHD_FLOW_QUEUE_L2CLEN_PTR(queue));

done:
	return ret;
}

int BCMFASTPATH
(dhd_flow_queue_enqueue_head)(dhd_pub_t *dhdp, flow_queue_t *queue, void *pkt)
{
	int ret = BCME_OK;

	ASSERT(queue != NULL);

	if (dhd_flow_queue_throttle(queue)) {
		queue->failures++;
		ret = (*queue->cb)(queue, pkt);
		goto done;
	}

	if (queue->head) {
		FLOW_QUEUE_PKT_SETNEXT(pkt, queue->head);
		queue->head = pkt;

	} else {
		queue->head = pkt;
		FLOW_QUEUE_PKT_SETNEXT(pkt, NULL);
		queue->tail = pkt; /* at tail */
	}

	queue->len++;
	/* increment parent's cummulative length */
	DHD_CUMM_CTR_INCR(DHD_FLOW_QUEUE_CLEN_PTR(queue));
	/* increment grandparent's cummulative length */
	DHD_CUMM_CTR_INCR(DHD_FLOW_QUEUE_L2CLEN_PTR(queue));

done:
	return ret;
}

/** Dequeue an 802.3 packet from a flow ring's queue, from head (FIFO) */
void *
BCMFASTPATH(dhd_flow_queue_dequeue)(dhd_pub_t *dhdp, flow_queue_t *queue)
{
	void * pkt;

	ASSERT(queue != NULL);

	pkt = queue->head; /* from head */

	if (pkt == NULL) {
		ASSERT((queue->len == 0) && (queue->tail == NULL));
		goto done;
	}

	queue->head = FLOW_QUEUE_PKT_NEXT(pkt);
	if (queue->head == NULL)
		queue->tail = NULL;

	queue->len--;
	/* decrement parent's cummulative length */
	DHD_CUMM_CTR_DECR(DHD_FLOW_QUEUE_CLEN_PTR(queue));
	/* decrement grandparent's cummulative length */
	DHD_CUMM_CTR_DECR(DHD_FLOW_QUEUE_L2CLEN_PTR(queue));

	FLOW_QUEUE_PKT_SETNEXT(pkt, NULL); /* dettach packet from queue */

done:
	return pkt;
}

/** Reinsert a dequeued 802.3 packet back at the head */
void
BCMFASTPATH(dhd_flow_queue_reinsert)(dhd_pub_t *dhdp, flow_queue_t *queue, void *pkt)
{
	if (queue->head == NULL) {
		queue->tail = pkt;
	}

	FLOW_QUEUE_PKT_SETNEXT(pkt, queue->head);
	queue->head = pkt;
	queue->len++;
	/* increment parent's cummulative length */
	DHD_CUMM_CTR_INCR(DHD_FLOW_QUEUE_CLEN_PTR(queue));
	/* increment grandparent's cummulative length */
	DHD_CUMM_CTR_INCR(DHD_FLOW_QUEUE_L2CLEN_PTR(queue));
}

/** Fetch the backup queue for a flowring, and assign flow control thresholds */
void
dhd_flow_ring_config_thresholds(dhd_pub_t *dhdp, uint16 flowid,
                     int queue_budget, int cumm_threshold, void *cumm_ctr,
                     int l2cumm_threshold, void *l2cumm_ctr)
{
	flow_queue_t * queue = NULL;

	ASSERT(dhdp != (dhd_pub_t*)NULL);
	ASSERT(queue_budget > 1);
	ASSERT(cumm_threshold > 1);
	ASSERT(cumm_ctr != (void*)NULL);
	ASSERT(l2cumm_threshold > 1);
	ASSERT(l2cumm_ctr != (void*)NULL);

	queue = dhd_flow_queue(dhdp, flowid);
	if (queue) {
		DHD_FLOW_QUEUE_SET_MAX(queue, queue_budget); /* Max queue length */

		/* Set the queue's parent threshold and cummulative counter */
		DHD_FLOW_QUEUE_SET_THRESHOLD(queue, cumm_threshold);
		DHD_FLOW_QUEUE_SET_CLEN(queue, cumm_ctr);

		/* Set the queue's grandparent threshold and cummulative counter */
		DHD_FLOW_QUEUE_SET_L2THRESHOLD(queue, l2cumm_threshold);
		DHD_FLOW_QUEUE_SET_L2CLEN(queue, l2cumm_ctr);
	}
}

/*
 * This function returns total number of flowrings that can be created for a INFRA STA.
 * For prio2ac mapping, it will return 4, prio2ac[8] = { 0, 1, 1, 0, 2, 2, 3, 3 }
 * For prio2tid mapping, it will return 8, prio2tid[8] = { 0, 1, 2, 3, 4, 5, 6, 7 }
 */
uint8
dhd_num_prio_supported_per_flow_ring(dhd_pub_t *dhdp)
{
	uint8 prio_count = 0;
	int i;
	/* Pick all elements one by one */
	for (i = 0; i < NUMPRIO; i++)
	{
		/* Check if the picked element is already counted */
		int j;
		for (j = 0; j < i; j++) {
			if (dhdp->flow_prio_map[i] == dhdp->flow_prio_map[j]) {
				break;
			}
		}
		/* If not counted earlier, then count it */
		if (i == j) {
			prio_count++;
		}
	}

	return prio_count;
}

uint8
dhd_get_max_multi_client_flow_rings(dhd_pub_t *dhdp)
{
	uint8 reserved_infra_sta_flow_rings = dhd_num_prio_supported_per_flow_ring(dhdp);
	uint8 total_tx_flow_rings = (uint8)dhd_get_max_flow_rings(dhdp);
	uint8 max_multi_client_flow_rings = total_tx_flow_rings - reserved_infra_sta_flow_rings;
	return max_multi_client_flow_rings;
}

int
dhd_flowid_map_init(dhd_pub_t *dhdp, uint16 max_tx_flow_rings)
{
#if defined(DHD_HTPUT_TUNABLES)
	uint16 max_normal_tx_flow_rings = max_tx_flow_rings - HTPUT_TOTAL_FLOW_RINGS;
#else
	uint16 max_normal_tx_flow_rings = max_tx_flow_rings;
#endif /* DHD_HTPUT_TUNABLES */

	/* Construct a normal flowid allocator from FLOWID_RESERVED to
	 * (max_normal_tx_flow_rings - 1)
	 */
	dhdp->flowid_allocator = id16_map_init(dhdp->osh, max_normal_tx_flow_rings,
		FLOWID_RESERVED);
	if (dhdp->flowid_allocator == NULL) {
		DHD_ERROR(("%s: flowid allocator init failure\n", __FUNCTION__));
		return BCME_NOMEM;
	}

#if defined(DHD_HTPUT_TUNABLES)
	if (HTPUT_TOTAL_FLOW_RINGS > 0) {
		dhdp->htput_flow_ring_start = max_normal_tx_flow_rings + FLOWID_RESERVED;
		/* Construct a htput flowid allocator from htput_flow_ring_start to
		 * (htput_flow_ring_start + HTPUT_TOTAL_FLOW_RINGS - 1)
		 */
		dhdp->htput_flowid_allocator = id16_map_init(dhdp->osh, HTPUT_TOTAL_FLOW_RINGS,
			dhdp->htput_flow_ring_start);
		if (dhdp->htput_flowid_allocator == NULL) {
			DHD_ERROR(("%s: htput flowid allocator init failure\n", __FUNCTION__));
			return BCME_NOMEM;
		}
		dhdp->htput_client_flow_rings = 0u;
	}
#endif /* !DHD_HTPUT_TUNABLES */

	return BCME_OK;
}

void
dhd_flowid_map_deinit(dhd_pub_t *dhdp)
{
	if (dhdp->flowid_allocator) {
		dhdp->flowid_allocator = id16_map_fini(dhdp->osh, dhdp->flowid_allocator);
	}
	ASSERT(dhdp->flowid_allocator == NULL);

#if defined(DHD_HTPUT_TUNABLES)
	if (dhdp->htput_flowid_allocator) {
		dhdp->htput_flowid_allocator = id16_map_fini(dhdp->osh,
			dhdp->htput_flowid_allocator);
		ASSERT(dhdp->htput_flowid_allocator == NULL);
	}
	dhdp->htput_client_flow_rings = 0u;
#endif /* !DHD_HTPUT_TUNABLES */
	return;
}

/** Initializes data structures of multiple flow rings
 * num_h2d_rings - max_h2d_rings including static and dynamic rings
 */
int
dhd_flow_rings_init(dhd_pub_t *dhdp, uint32 num_h2d_rings)
{
	uint32 idx;
	uint32 flow_ring_table_sz = 0;
	uint32 if_flow_lkup_sz = 0;
	flow_ring_table_t *flow_ring_table = NULL;
	if_flow_lkup_t *if_flow_lkup = NULL;
	void *lock = NULL;
	void *list_lock = NULL;
	unsigned long flags;
	uint16 max_tx_flow_rings;

	DHD_INFO(("%s\n", __FUNCTION__));

	/*
	 * Only 16-bit flowid map will be allocated for actual number of Tx flowrings
	 * excluding common rings.
	 * Rest all flowring data structure will be allocated for all num_h2d_rings.
	 */
	max_tx_flow_rings = dhd_get_max_flow_rings(dhdp);
	if (dhd_flowid_map_init(dhdp, max_tx_flow_rings) != BCME_OK) {
		DHD_ERROR(("%s: dhd_flowid_map_init failure\n", __FUNCTION__));
		goto fail;
	}

	/* Any Tx flow id should not be > max_tx_flowid */
	dhdp->max_tx_flowid = max_tx_flow_rings + FLOWID_RESERVED - 1;

	/* Allocate a flow ring table, comprising of requested number of rings */
	flow_ring_table_sz = (num_h2d_rings * sizeof(flow_ring_node_t));
	flow_ring_table = (flow_ring_table_t *)MALLOCZ(dhdp->osh, flow_ring_table_sz);
	if (flow_ring_table == NULL) {
		DHD_ERROR(("%s: flow ring table alloc failure\n", __FUNCTION__));
		goto fail;
	}

	/* Initialize flow ring table state */
	DHD_CUMM_CTR_INIT(&dhdp->cumm_ctr);
	DHD_CUMM_CTR_INIT(&dhdp->l2cumm_ctr);
	bzero((uchar *)flow_ring_table, flow_ring_table_sz);
	for (idx = 0; idx < num_h2d_rings; idx++) {
		flow_ring_table[idx].status = FLOW_RING_STATUS_CLOSED;
		flow_ring_table[idx].flowid = (uint16)idx;
		flow_ring_table[idx].lock = osl_spin_lock_init(dhdp->osh);
#ifdef IDLE_TX_FLOW_MGMT
		flow_ring_table[idx].last_active_ts = OSL_SYSUPTIME();
#endif /* IDLE_TX_FLOW_MGMT */
		if (flow_ring_table[idx].lock == NULL) {
			DHD_ERROR(("%s: Failed to init spinlock for queue!\n", __FUNCTION__));
			goto fail;
		}

		dll_init(&flow_ring_table[idx].list);

		/* Initialize the per flow ring backup queue */
		dhd_flow_queue_init(dhdp, &flow_ring_table[idx].queue,
		                    dhdp->conf->flow_ring_queue_threshold);
	}

	/* Allocate per interface hash table (for fast lookup from interface to flow ring) */
	if_flow_lkup_sz = sizeof(if_flow_lkup_t) * DHD_MAX_IFS;
	if_flow_lkup = (if_flow_lkup_t *)DHD_OS_PREALLOC(dhdp,
		DHD_PREALLOC_IF_FLOW_LKUP, if_flow_lkup_sz);
	if (if_flow_lkup == NULL) {
		DHD_ERROR(("%s: if flow lkup alloc failure\n", __FUNCTION__));
		goto fail;
	}

	/* Initialize per interface hash table */
	for (idx = 0; idx < DHD_MAX_IFS; idx++) {
		int hash_ix;
		if_flow_lkup[idx].status = 0;
		if_flow_lkup[idx].role = 0;
		for (hash_ix = 0; hash_ix < DHD_FLOWRING_HASH_SIZE; hash_ix++)
			if_flow_lkup[idx].fl_hash[hash_ix] = NULL;
	}

	lock = osl_spin_lock_init(dhdp->osh);
	if (lock == NULL)
		goto fail;

	list_lock = osl_spin_lock_init(dhdp->osh);
	if (list_lock == NULL)
		goto lock_fail;

	dhdp->flow_prio_map_type = DHD_FLOW_PRIO_AC_MAP;
	bcopy(prio2ac, dhdp->flow_prio_map, sizeof(uint8) * NUMPRIO);

	dhdp->max_multi_client_flow_rings = dhd_get_max_multi_client_flow_rings(dhdp);
	dhdp->multi_client_flow_rings = 0U;

#ifdef DHD_LOSSLESS_ROAMING
	dhdp->dequeue_prec_map = ALLPRIO;
#endif
	/* Now populate into dhd pub */
	DHD_FLOWID_LOCK(lock, flags);
	dhdp->num_h2d_rings = num_h2d_rings;
	dhdp->flow_ring_table = (void *)flow_ring_table;
	dhdp->if_flow_lkup = (void *)if_flow_lkup;
	dhdp->flowid_lock = lock;
	dhdp->flow_rings_inited = TRUE;
	dhdp->flowring_list_lock = list_lock;
	DHD_FLOWID_UNLOCK(lock, flags);

	DHD_INFO(("%s done\n", __FUNCTION__));
	return BCME_OK;

lock_fail:
	/* deinit the spinlock */
	osl_spin_lock_deinit(dhdp->osh, lock);

fail:
	/* Destruct the per interface flow lkup table */
	if (if_flow_lkup != NULL) {
		DHD_OS_PREFREE(dhdp, if_flow_lkup, if_flow_lkup_sz);
	}
	if (flow_ring_table != NULL) {
		for (idx = 0; idx < num_h2d_rings; idx++) {
			if (flow_ring_table[idx].lock != NULL)
				osl_spin_lock_deinit(dhdp->osh, flow_ring_table[idx].lock);
		}
		MFREE(dhdp->osh, flow_ring_table, flow_ring_table_sz);
	}
	dhd_flowid_map_deinit(dhdp);

	return BCME_NOMEM;
}

/** Deinit Flow Ring specific data structures */
void dhd_flow_rings_deinit(dhd_pub_t *dhdp)
{
	uint16 idx;
	uint32 flow_ring_table_sz;
	uint32 if_flow_lkup_sz;
	flow_ring_table_t *flow_ring_table;
	unsigned long flags;
	void *lock;

	DHD_INFO(("dhd_flow_rings_deinit\n"));

	if (!(dhdp->flow_rings_inited)) {
		DHD_ERROR(("dhd_flow_rings not initialized!\n"));
		return;
	}

	if (dhdp->flow_ring_table != NULL) {

		ASSERT(dhdp->num_h2d_rings > 0);

		DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);
		flow_ring_table = (flow_ring_table_t *)dhdp->flow_ring_table;
		dhdp->flow_ring_table = NULL;
		DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);
		for (idx = 0; idx < dhdp->num_h2d_rings; idx++) {
			if (flow_ring_table[idx].active) {
				dhd_bus_clean_flow_ring(dhdp->bus, &flow_ring_table[idx]);
			}
			ASSERT(DHD_FLOW_QUEUE_EMPTY(&flow_ring_table[idx].queue));

			/* Deinit flow ring queue locks before destroying flow ring table */
			if (flow_ring_table[idx].lock != NULL) {
				osl_spin_lock_deinit(dhdp->osh, flow_ring_table[idx].lock);
			}
			flow_ring_table[idx].lock = NULL;

		}

		/* Destruct the flow ring table */
		flow_ring_table_sz = dhdp->num_h2d_rings * sizeof(flow_ring_table_t);
		MFREE(dhdp->osh, flow_ring_table, flow_ring_table_sz);
	}

	DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);

	/* Destruct the per interface flow lkup table */
	if (dhdp->if_flow_lkup != NULL) {
		if_flow_lkup_sz = sizeof(if_flow_lkup_t) * DHD_MAX_IFS;
		bzero((uchar *)dhdp->if_flow_lkup, if_flow_lkup_sz);
		DHD_OS_PREFREE(dhdp, dhdp->if_flow_lkup, if_flow_lkup_sz);
		dhdp->if_flow_lkup = NULL;
	}

	/* Destruct the flowid allocator */
	dhd_flowid_map_deinit(dhdp);

	dhdp->num_h2d_rings = 0U;
	bzero(dhdp->flow_prio_map, sizeof(uint8) * NUMPRIO);

	dhdp->max_multi_client_flow_rings = 0U;
	dhdp->multi_client_flow_rings = 0U;

	lock = dhdp->flowid_lock;
	dhdp->flowid_lock = NULL;

	if (lock) {
		DHD_FLOWID_UNLOCK(lock, flags);
		osl_spin_lock_deinit(dhdp->osh, lock);
	}

	osl_spin_lock_deinit(dhdp->osh, dhdp->flowring_list_lock);
	dhdp->flowring_list_lock = NULL;

	ASSERT(dhdp->if_flow_lkup == NULL);
	ASSERT(dhdp->flow_ring_table == NULL);
	dhdp->flow_rings_inited = FALSE;
}

/** Uses hash table to quickly map from ifindex to a flow ring 'role' (STA/AP) */
uint8
dhd_flow_rings_ifindex2role(dhd_pub_t *dhdp, uint8 ifindex)
{
	if_flow_lkup_t *if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;
	ASSERT(if_flow_lkup);
	return if_flow_lkup[ifindex].role;
}

#ifdef WLTDLS
bool is_tdls_destination(dhd_pub_t *dhdp, uint8 *da)
{
	unsigned long flags;
	tdls_peer_node_t *cur = NULL;

	DHD_TDLS_LOCK(&dhdp->tdls_lock, flags);
	/* Check only if tdls peer is added */
	if (dhdp->peer_tbl.tdls_peer_count && !(ETHER_ISMULTI(da))) {
		cur = dhdp->peer_tbl.node;

		while (cur != NULL) {
			if (!memcmp(da, cur->addr, ETHER_ADDR_LEN)) {
				DHD_TDLS_UNLOCK(&dhdp->tdls_lock, flags);
				return TRUE;
			}
			cur = cur->next;
		}
	}
	DHD_TDLS_UNLOCK(&dhdp->tdls_lock, flags);
	return FALSE;
}
#endif /* WLTDLS */

/** Uses hash table to quickly map from ifindex+prio+da to a flow ring id */
static INLINE uint16
dhd_flowid_find(dhd_pub_t *dhdp, uint8 ifindex, uint8 prio, char *sa, char *da)
{
	int hash;
	bool ismcast = FALSE;
	flow_hash_info_t *cur;
	if_flow_lkup_t *if_flow_lkup;
	unsigned long flags;

	ASSERT(ifindex < DHD_MAX_IFS);
	if (ifindex >= DHD_MAX_IFS)
		return FLOWID_INVALID;

	DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);
	if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;

	ASSERT(if_flow_lkup);

	if (DHD_IF_ROLE_GENERIC_STA(dhdp, ifindex)) {
#ifdef WLTDLS
		if (is_tdls_destination(dhdp, da)) {
			hash = DHD_FLOWRING_HASHINDEX(da, prio);
			cur = if_flow_lkup[ifindex].fl_hash[hash];
			while (cur != NULL) {
				if (!memcmp(cur->flow_info.da, da, ETHER_ADDR_LEN)) {
					DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);
					return cur->flowid;
				}
				cur = cur->next;
			}
			DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);
			return FLOWID_INVALID;
		}
#endif /* WLTDLS */
		/* For STA non TDLS dest and WDS dest flow ring id is mapped based on prio only */
		cur = if_flow_lkup[ifindex].fl_hash[prio];
		if (cur) {
			DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);
			return cur->flowid;
		}
	} else {

		if (ETHER_ISMULTI(da)) {
			ismcast = TRUE;
			hash = 0;
		} else {
			hash = DHD_FLOWRING_HASHINDEX(da, prio);
		}

		cur = if_flow_lkup[ifindex].fl_hash[hash];

		while (cur) {
			if ((ismcast && ETHER_ISMULTI(cur->flow_info.da)) ||
				(!memcmp(cur->flow_info.da, da, ETHER_ADDR_LEN) &&
				(cur->flow_info.tid == prio))) {
				DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);
				return cur->flowid;
			}
			cur = cur->next;
		}
	}
	DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);

#ifdef DHD_EFI
	DHD_TRACE(("%s: cannot find flowid\n", __FUNCTION__));
#else
	DHD_FLOWRING_INFO(("%s: cannot find flowid\n", __FUNCTION__));
#endif
	return FLOWID_INVALID;
} /* dhd_flowid_find */

static uint16
dhd_flowid_map_alloc(dhd_pub_t *dhdp, uint8 ifindex, uint8 prio, char *da)
{
	uint16 flowid = FLOWID_INVALID;
	ASSERT(dhdp->flowid_allocator != NULL);

#if defined(DHD_HTPUT_TUNABLES)
	if (dhdp->htput_flowid_allocator) {
		if (prio == HTPUT_FLOW_RING_PRIO) {
			if (DHD_IF_ROLE_GENERIC_STA(dhdp, ifindex)) {
				/* For STA case, only one flowring per PRIO is created,
				 * so no need to have a HTPUT counter variable for STA case.
				 * If already HTPUT flowring is allocated for given HTPUT_PRIO,
				 * then this function will not even get called as dhd_flowid_find
				 * will take care assigning same for those HTPUT_PRIO packets.
				 */
				flowid = id16_map_alloc(dhdp->htput_flowid_allocator);
			} else if (DHD_IF_ROLE_MULTI_CLIENT(dhdp, ifindex) && !ETHER_ISMULTI(da)) {
				/* Use HTPUT flowrings for only HTPUT_NUM_CLIENT_FLOW_RINGS */
				if (dhdp->htput_client_flow_rings < HTPUT_NUM_CLIENT_FLOW_RINGS) {
					flowid = id16_map_alloc(dhdp->htput_flowid_allocator);
					/* increment htput client counter */
					if (flowid != FLOWID_INVALID) {
						dhdp->htput_client_flow_rings++;
					}
				}
			}
		}
	}
#endif /* !DHD_HTPUT_TUNABLES */

	BCM_REFERENCE(flowid);

	/*
	 * For HTPUT case, if the high throughput flowrings are already allocated
	 * for the given role, the control comes here.
	 */
	if (flowid == FLOWID_INVALID) {
		flowid = id16_map_alloc(dhdp->flowid_allocator);
	}

	return flowid;
}

/** Create unique Flow ID, called when a flow ring is created. */
static INLINE uint16
dhd_flowid_alloc(dhd_pub_t *dhdp, uint8 ifindex, uint8 prio, char *sa, char *da)
{
	flow_hash_info_t *fl_hash_node, *cur;
	if_flow_lkup_t *if_flow_lkup;
	int hash;
	uint16 flowid;
	unsigned long flags;

	fl_hash_node = (flow_hash_info_t *) MALLOCZ(dhdp->osh, sizeof(flow_hash_info_t));
	if (fl_hash_node == NULL) {
		DHD_ERROR(("%s: flow_hash_info_t memory allocation failed \n", __FUNCTION__));
		return FLOWID_INVALID;
	}
	memcpy(fl_hash_node->flow_info.da, da, sizeof(fl_hash_node->flow_info.da));

	DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);
	flowid = dhd_flowid_map_alloc(dhdp, ifindex, prio, da);
	DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);

	if (flowid == FLOWID_INVALID) {
		MFREE(dhdp->osh, fl_hash_node,  sizeof(flow_hash_info_t));
		DHD_ERROR_RLMT(("%s: cannot get free flowid \n", __FUNCTION__));
		return FLOWID_INVALID;
	}

	fl_hash_node->flowid = flowid;
	fl_hash_node->flow_info.tid = prio;
	fl_hash_node->flow_info.ifindex = ifindex;
	fl_hash_node->next = NULL;

	DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);
	if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;

	if (DHD_IF_ROLE_GENERIC_STA(dhdp, ifindex)) {
		/* For STA/GC non TDLS dest and WDS dest we allocate entry based on prio only */
#ifdef WLTDLS
		if (is_tdls_destination(dhdp, da)) {
			hash = DHD_FLOWRING_HASHINDEX(da, prio);
			cur = if_flow_lkup[ifindex].fl_hash[hash];
			if (cur) {
				while (cur->next) {
					cur = cur->next;
				}
				cur->next = fl_hash_node;
			} else {
				if_flow_lkup[ifindex].fl_hash[hash] = fl_hash_node;
			}
		} else
#endif /* WLTDLS */
			if_flow_lkup[ifindex].fl_hash[prio] = fl_hash_node;
	} else {

		/* For bcast/mcast assign first slot in in interface */
		hash = ETHER_ISMULTI(da) ? 0 : DHD_FLOWRING_HASHINDEX(da, prio);
		cur = if_flow_lkup[ifindex].fl_hash[hash];
		if (cur) {
			while (cur->next) {
				cur = cur->next;
			}
			cur->next = fl_hash_node;
		} else
			if_flow_lkup[ifindex].fl_hash[hash] = fl_hash_node;
	}
	DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);

	DHD_FLOWRING_INFO(("%s: allocated flowid %d\n", __FUNCTION__, fl_hash_node->flowid));

	if (fl_hash_node->flowid > dhdp->max_tx_flowid) {
		DHD_ERROR(("%s: flowid=%d max_tx_flowid=%d ifindex=%d prio=%d role=%d\n",
			__FUNCTION__, fl_hash_node->flowid, dhdp->max_tx_flowid,
			ifindex, prio, if_flow_lkup[ifindex].role));
		dhd_prhex("da", (uchar *)da, ETHER_ADDR_LEN, DHD_ERROR_VAL);
		dhd_prhex("sa", (uchar *)sa, ETHER_ADDR_LEN, DHD_ERROR_VAL);
		return FLOWID_INVALID;
	}

	return fl_hash_node->flowid;
} /* dhd_flowid_alloc */

/** Get flow ring ID, if not present try to create one */
static INLINE int
dhd_flowid_lookup(dhd_pub_t *dhdp, uint8 ifindex,
                  uint8 prio, char *sa, char *da, uint16 *flowid)
{
	uint16 id;
	flow_ring_node_t *flow_ring_node;
	flow_ring_table_t *flow_ring_table;
	unsigned long flags;
	int ret;

	DHD_TRACE(("%s\n", __FUNCTION__));

	if (!dhdp->flow_ring_table) {
		return BCME_ERROR;
	}

	ASSERT(ifindex < DHD_MAX_IFS);
	if (ifindex >= DHD_MAX_IFS)
		return BCME_BADARG;

	flow_ring_table = (flow_ring_table_t *)dhdp->flow_ring_table;

	id = dhd_flowid_find(dhdp, ifindex, prio, sa, da);

	if (id == FLOWID_INVALID) {
		bool if_role_multi_client;
		if_flow_lkup_t *if_flow_lkup;
		if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;

		if (!if_flow_lkup[ifindex].status)
			return BCME_ERROR;

		/* check role for multi client case */
		if_role_multi_client = DHD_IF_ROLE_MULTI_CLIENT(dhdp, ifindex);

		/* Abort Flowring creation if multi client flowrings crossed the threshold */
#ifdef DHD_LIMIT_MULTI_CLIENT_FLOWRINGS
		if (if_role_multi_client &&
			(dhdp->multi_client_flow_rings >= dhdp->max_multi_client_flow_rings)) {
			DHD_ERROR_RLMT(("%s: Max multi client flow rings reached: %d:%d\n",
				__FUNCTION__, dhdp->multi_client_flow_rings,
				dhdp->max_multi_client_flow_rings));
			return BCME_ERROR;
		}
#endif /* DHD_LIMIT_MULTI_CLIENT_FLOWRINGS */

		/* Do not create Flowring if peer is not associated */
#if (defined(linux) || defined(LINUX)) && defined(PCIE_FULL_DONGLE)
		if (if_role_multi_client && !ETHER_ISMULTI(da) &&
			!dhd_sta_associated(dhdp, ifindex, (uint8 *)da)) {
			DHD_ERROR_RLMT(("%s: Skip send pkt without peer addition\n", __FUNCTION__));
			return BCME_ERROR;
		}
#endif /* (linux || LINUX) && PCIE_FULL_DONGLE */

		id = dhd_flowid_alloc(dhdp, ifindex, prio, sa, da);
		if (id == FLOWID_INVALID) {
			DHD_ERROR_RLMT(("%s: alloc flowid ifindex %u status %u\n",
				__FUNCTION__, ifindex, if_flow_lkup[ifindex].status));
			return BCME_ERROR;
		}

		ASSERT(id <= dhdp->max_tx_flowid);

		/* Only after flowid alloc, increment multi_client_flow_rings */
		if (if_role_multi_client) {
			dhdp->multi_client_flow_rings++;
		}

		/* register this flowid in dhd_pub */
		dhd_add_flowid(dhdp, ifindex, prio, da, id);

		flow_ring_node = (flow_ring_node_t *) &flow_ring_table[id];

		DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);

		/* Init Flow info */
		memcpy(flow_ring_node->flow_info.sa, sa, sizeof(flow_ring_node->flow_info.sa));
		memcpy(flow_ring_node->flow_info.da, da, sizeof(flow_ring_node->flow_info.da));
		flow_ring_node->flow_info.tid = prio;
		flow_ring_node->flow_info.ifindex = ifindex;
		flow_ring_node->active = TRUE;
		flow_ring_node->status = FLOW_RING_STATUS_CREATE_PENDING;

#ifdef DEVICE_TX_STUCK_DETECT
		flow_ring_node->tx_cmpl = flow_ring_node->tx_cmpl_prev = OSL_SYSUPTIME();
		flow_ring_node->stuck_count = 0;
#endif /* DEVICE_TX_STUCK_DETECT */
#ifdef TX_STATUS_LATENCY_STATS
		flow_ring_node->flow_info.num_tx_status = 0;
		flow_ring_node->flow_info.cum_tx_status_latency = 0;
		flow_ring_node->flow_info.num_tx_pkts = 0;
#endif /* TX_STATUS_LATENCY_STATS */
#ifdef BCMDBG
		bzero(&flow_ring_node->flow_info.tx_status[0],
			sizeof(uint32) * DHD_MAX_TX_STATUS_MSGS);
#endif
		DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);

		/* Create and inform device about the new flow */
		if (dhd_bus_flow_ring_create_request(dhdp->bus, (void *)flow_ring_node)
				!= BCME_OK) {
			DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);
			flow_ring_node->status = FLOW_RING_STATUS_CLOSED;
			flow_ring_node->active = FALSE;
			DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
			DHD_ERROR(("%s: create error %d\n", __FUNCTION__, id));
			return BCME_ERROR;
		}

		*flowid = id;
		return BCME_OK;
	} else {
		/* if the Flow id was found in the hash */

		if (id > dhdp->max_tx_flowid) {
			DHD_ERROR(("%s: Invalid flow id : %u, max_tx_flowid : %u\n",
				__FUNCTION__, id, dhdp->max_tx_flowid));
			*flowid = FLOWID_INVALID;
			ASSERT(0);
			return BCME_ERROR;
		}

		flow_ring_node = (flow_ring_node_t *) &flow_ring_table[id];
		DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);

		/*
		 * If the flow_ring_node is in Open State or Status pending state then
		 * we can return the Flow id to the caller.If the flow_ring_node is in
		 * FLOW_RING_STATUS_PENDING this means the creation is in progress and
		 * hence the packets should be queued.
		 *
		 * If the flow_ring_node is in FLOW_RING_STATUS_DELETE_PENDING Or
		 * FLOW_RING_STATUS_CLOSED, then we should return Error.
		 * Note that if the flowing is being deleted we would mark it as
		 * FLOW_RING_STATUS_DELETE_PENDING.  Now before Dongle could respond and
		 * before we mark it as FLOW_RING_STATUS_CLOSED we could get tx packets.
		 * We should drop the packets in that case.
		 * The decission to return OK should NOT be based on 'active' variable, beause
		 * active is made TRUE when a flow_ring_node gets allocated and is made
		 * FALSE when the flow ring gets removed and does not reflect the True state
		 * of the Flow ring.
		 * In case if IDLE_TX_FLOW_MGMT is defined, we have to handle two more flowring
		 * states. If the flow_ring_node's status is FLOW_RING_STATUS_SUSPENDED, the flowid
		 * is to be returned and from dhd_bus_txdata, the flowring would be resumed again.
		 * The status FLOW_RING_STATUS_RESUME_PENDING, is equivalent to
		 * FLOW_RING_STATUS_CREATE_PENDING.
		 */
		if (flow_ring_node->status == FLOW_RING_STATUS_DELETE_PENDING ||
			flow_ring_node->status == FLOW_RING_STATUS_CLOSED) {
			*flowid = FLOWID_INVALID;
			ret = BCME_ERROR;
		} else {
			*flowid = id;
			ret = BCME_OK;
		}

		DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
		return ret;
	} /* Flow Id found in the hash */
} /* dhd_flowid_lookup */

int
dhd_flowid_find_by_ifidx(dhd_pub_t *dhdp, uint8 ifindex, uint16 flowid)
{
	int hashidx = 0;
	bool found = FALSE;
	flow_hash_info_t *cur;
	if_flow_lkup_t *if_flow_lkup;
	unsigned long flags;

	if (!dhdp->flow_ring_table) {
		DHD_ERROR(("%s : dhd->flow_ring_table is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);
	if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;
	for (hashidx = 0; hashidx < DHD_FLOWRING_HASH_SIZE; hashidx++) {
		cur = if_flow_lkup[ifindex].fl_hash[hashidx];
		if (cur) {
			if (cur->flowid == flowid) {
				found = TRUE;
			}

			while (!found && cur) {
				if (cur->flowid == flowid) {
					found = TRUE;
					break;
				}
				cur = cur->next;
			}

			if (found) {
				DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);
				return BCME_OK;
			}
		}
	}
	DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);

	return BCME_ERROR;
}

int
dhd_flowid_debug_create(dhd_pub_t *dhdp, uint8 ifindex,
	uint8 prio, char *sa, char *da, uint16 *flowid)
{
	return dhd_flowid_lookup(dhdp, ifindex, prio, sa, da, flowid);
}

/**
 * Assign existing or newly created flowid to an 802.3 packet. This flowid is later on used to
 * select the flowring to send the packet to the dongle.
 */
int
BCMFASTPATH(dhd_flowid_update)(dhd_pub_t *dhdp, uint8 ifindex, uint8 prio, void *pktbuf)
{
	uint8 *pktdata = (uint8 *)PKTDATA(dhdp->osh, pktbuf);
	struct ether_header *eh = (struct ether_header *)pktdata;
	uint16 flowid = 0;

	ASSERT(ifindex < DHD_MAX_IFS);

	if (ifindex >= DHD_MAX_IFS) {
		return BCME_BADARG;
	}

	if (!dhdp->flowid_allocator) {
		DHD_ERROR(("%s: Flow ring not intited yet  \n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (dhd_flowid_lookup(dhdp, ifindex, prio, (char *)eh->ether_shost, (char *)eh->ether_dhost,
		&flowid) != BCME_OK) {
		return BCME_ERROR;
	}

	DHD_FLOWRING_INFO(("%s: prio %d flowid %d\n", __FUNCTION__, prio, flowid));

	/* Tag the packet with flowid */
	DHD_PKT_SET_FLOWID(pktbuf, flowid);
	return BCME_OK;
}

static void
dhd_flowid_map_free(dhd_pub_t *dhdp, uint8 ifindex, uint16 flowid)
{
#if defined(DHD_HTPUT_TUNABLES)
	if (dhdp->htput_flowid_allocator) {
		if (DHD_IS_FLOWID_HTPUT(dhdp, flowid)) {
			id16_map_free(dhdp->htput_flowid_allocator, flowid);
			/* decrement htput client counter */
			if (DHD_IF_ROLE_MULTI_CLIENT(dhdp, ifindex)) {
				dhdp->htput_client_flow_rings--;
			}
			return;
		}
	}
#endif /* !DHD_HTPUT_TUNABLES */

	id16_map_free(dhdp->flowid_allocator, flowid);

	return;
}

void
dhd_flowid_free(dhd_pub_t *dhdp, uint8 ifindex, uint16 flowid)
{
	int hashix;
	bool found = FALSE;
	flow_hash_info_t *cur, *prev;
	if_flow_lkup_t *if_flow_lkup;
	unsigned long flags;
	bool if_role_multi_client;

	ASSERT(ifindex < DHD_MAX_IFS);
	if (ifindex >= DHD_MAX_IFS)
		return;

	DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);
	if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;

	if_role_multi_client = DHD_IF_ROLE_MULTI_CLIENT(dhdp, ifindex);

	for (hashix = 0; hashix < DHD_FLOWRING_HASH_SIZE; hashix++) {

		cur = if_flow_lkup[ifindex].fl_hash[hashix];

		if (cur) {
			if (cur->flowid == flowid) {
				found = TRUE;
			}

			prev = NULL;
			while (!found && cur) {
				if (cur->flowid == flowid) {
					found = TRUE;
					break;
				}
				prev = cur;
				cur = cur->next;
			}
			if (found) {
				if (!prev) {
					if_flow_lkup[ifindex].fl_hash[hashix] = cur->next;
				} else {
					prev->next = cur->next;
				}

				/* Decrement multi_client_flow_rings */
				if (if_role_multi_client) {
					dhdp->multi_client_flow_rings--;
				}

				/* deregister flowid from dhd_pub. */
				dhd_del_flowid(dhdp, ifindex, flowid);

				dhd_flowid_map_free(dhdp, ifindex, flowid);
				DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);
				MFREE(dhdp->osh, cur, sizeof(flow_hash_info_t));

				return;
			}
		}
	}

	DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);
	DHD_ERROR(("%s: could not free flow ring hash entry flowid %d\n",
	           __FUNCTION__, flowid));
} /* dhd_flowid_free */

/**
 * Delete all Flow rings associated with the given interface. Is called when eg the dongle
 * indicates that a wireless link has gone down.
 */
void
dhd_flow_rings_delete(dhd_pub_t *dhdp, uint8 ifindex)
{
	uint32 id;
	flow_ring_table_t *flow_ring_table;

	DHD_ERROR(("%s: ifindex %u\n", __FUNCTION__, ifindex));

	ASSERT(ifindex < DHD_MAX_IFS);
	if (ifindex >= DHD_MAX_IFS)
		return;

	if (!dhdp->flow_ring_table)
		return;

	flow_ring_table = (flow_ring_table_t *)dhdp->flow_ring_table;
	for (id = 0; id < dhdp->num_h2d_rings; id++) {
		if (flow_ring_table[id].active &&
			(flow_ring_table[id].flow_info.ifindex == ifindex) &&
			(flow_ring_table[id].status == FLOW_RING_STATUS_OPEN)) {
			dhd_bus_flow_ring_delete_request(dhdp->bus,
			                                 (void *) &flow_ring_table[id]);
		}
	}
}

void
dhd_flow_rings_flush(dhd_pub_t *dhdp, uint8 ifindex)
{
	uint32 id;
	flow_ring_table_t *flow_ring_table;

	DHD_INFO(("%s: ifindex %u\n", __FUNCTION__, ifindex));

	ASSERT(ifindex < DHD_MAX_IFS);
	if (ifindex >= DHD_MAX_IFS)
		return;

	if (!dhdp->flow_ring_table)
		return;
	flow_ring_table = (flow_ring_table_t *)dhdp->flow_ring_table;

	for (id = 0; id < dhdp->num_h2d_rings; id++) {
		if (flow_ring_table[id].active &&
			(flow_ring_table[id].flow_info.ifindex == ifindex) &&
			(flow_ring_table[id].status == FLOW_RING_STATUS_OPEN)) {
			dhd_bus_flow_ring_flush_request(dhdp->bus,
			                                 (void *) &flow_ring_table[id]);
		}
	}
}

/** Delete flow ring(s) for given peer address. */
void
dhd_flow_rings_delete_for_peer(dhd_pub_t *dhdp, uint8 ifindex, char *addr)
{
	uint32 id;
	flow_ring_table_t *flow_ring_table;

	DHD_ERROR(("%s: ifindex %u\n", __FUNCTION__, ifindex));

	ASSERT(ifindex < DHD_MAX_IFS);
	if (ifindex >= DHD_MAX_IFS)
		return;

	if (!dhdp->flow_ring_table)
		return;

	flow_ring_table = (flow_ring_table_t *)dhdp->flow_ring_table;
	for (id = 0; id < dhdp->num_h2d_rings; id++) {
		/*
		 * Send flowring delete request even if flowring status is
		 * FLOW_RING_STATUS_CREATE_PENDING, to handle cases where DISASSOC_IND
		 * event comes ahead of flowring create response.
		 * Otherwise the flowring will not be deleted later as there will not be any
		 * DISASSOC_IND event. With this change, when create response event comes to DHD,
		 * it will change the status to FLOW_RING_STATUS_OPEN and soon delete response
		 * event will come, upon which DHD will delete the flowring.
		 */
		if (flow_ring_table[id].active &&
			(flow_ring_table[id].flow_info.ifindex == ifindex) &&
			(!memcmp(flow_ring_table[id].flow_info.da, addr, ETHER_ADDR_LEN)) &&
			((flow_ring_table[id].status == FLOW_RING_STATUS_OPEN) ||
			(flow_ring_table[id].status == FLOW_RING_STATUS_CREATE_PENDING))) {
			DHD_ERROR(("%s: deleting flowid %d\n",
				__FUNCTION__, flow_ring_table[id].flowid));
			dhd_bus_flow_ring_delete_request(dhdp->bus,
				(void *) &flow_ring_table[id]);
		}
	}
}

/** Handles interface ADD, CHANGE, DEL indications from the dongle */
void
dhd_update_interface_flow_info(dhd_pub_t *dhdp, uint8 ifindex,
                               uint8 op, uint8 role)
{
	if_flow_lkup_t *if_flow_lkup;
	unsigned long flags;

	ASSERT(ifindex < DHD_MAX_IFS);
	if (ifindex >= DHD_MAX_IFS)
		return;

	DHD_INFO(("%s: ifindex %u op %u role is %u \n",
	          __FUNCTION__, ifindex, op, role));
	if (!dhdp->flowid_allocator) {
		DHD_ERROR(("%s: Flow ring not intited yet  \n", __FUNCTION__));
		return;
	}

	DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);
	if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;

	if (op == WLC_E_IF_ADD || op == WLC_E_IF_CHANGE) {

		if_flow_lkup[ifindex].role = role;

		if (role == WLC_E_IF_ROLE_WDS) {
			/**
			 * WDS role does not send WLC_E_LINK event after interface is up.
			 * So to create flowrings for WDS, make status as TRUE in WLC_E_IF itself.
			 * same is true while making the status as FALSE.
			 * TODO: Fix FW to send WLC_E_LINK for WDS role aswell. So that all the
			 * interfaces are handled uniformly.
			 */
			if_flow_lkup[ifindex].status = TRUE;
			DHD_INFO(("%s: Mcast Flow ring for ifindex %d role is %d \n",
			          __FUNCTION__, ifindex, role));
		}
	} else	if ((op == WLC_E_IF_DEL) && (role == WLC_E_IF_ROLE_WDS)) {
		if_flow_lkup[ifindex].status = FALSE;
		DHD_INFO(("%s: cleanup all Flow rings for ifindex %d role is %d \n",
		          __FUNCTION__, ifindex, role));
	}
	DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);
}

/** Handles a STA 'link' indication from the dongle */
int
dhd_update_interface_link_status(dhd_pub_t *dhdp, uint8 ifindex, uint8 status)
{
	if_flow_lkup_t *if_flow_lkup;
	unsigned long flags;

	ASSERT(ifindex < DHD_MAX_IFS);
	if (ifindex >= DHD_MAX_IFS)
		return BCME_BADARG;

	DHD_INFO(("%s: ifindex %d status %d\n", __FUNCTION__, ifindex, status));

	DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);
	if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;

	if (status) {
		if_flow_lkup[ifindex].status = TRUE;
	} else {
		if_flow_lkup[ifindex].status = FALSE;
	}

	DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);

	return BCME_OK;
}

/** Update flow priority mapping, called on IOVAR */
int dhd_update_flow_prio_map(dhd_pub_t *dhdp, uint8 map)
{
	uint16 flowid;
	flow_ring_node_t *flow_ring_node;

	if (map > DHD_FLOW_PRIO_LLR_MAP)
		return BCME_BADOPTION;

	/* Check if we need to change prio map */
	if (map == dhdp->flow_prio_map_type)
		return BCME_OK;

	/* If any ring is active we cannot change priority mapping for flow rings */
	for (flowid = 0; flowid < dhdp->num_h2d_rings; flowid++) {
		flow_ring_node = DHD_FLOW_RING(dhdp, flowid);
		if (flow_ring_node->active)
			return BCME_EPERM;
	}

	/* Inform firmware about new mapping type */
	if (BCME_OK != dhd_flow_prio_map(dhdp, &map, TRUE))
		return BCME_ERROR;

	/* update internal structures */
	dhdp->flow_prio_map_type = map;
	if (dhdp->flow_prio_map_type == DHD_FLOW_PRIO_TID_MAP)
		bcopy(prio2tid, dhdp->flow_prio_map, sizeof(uint8) * NUMPRIO);
	else
		bcopy(prio2ac, dhdp->flow_prio_map, sizeof(uint8) * NUMPRIO);

	dhdp->max_multi_client_flow_rings = dhd_get_max_multi_client_flow_rings(dhdp);

	return BCME_OK;
}

/** Inform firmware on updated flow priority mapping, called on IOVAR */
int dhd_flow_prio_map(dhd_pub_t *dhd, uint8 *map, bool set)
{
	uint8 iovbuf[WLC_IOCTL_SMLEN];
	int len;
	uint32 val;
	if (!set) {
		bzero(&iovbuf, sizeof(iovbuf));
		len = bcm_mkiovar("bus:fl_prio_map", NULL, 0, (char*)iovbuf, sizeof(iovbuf));
		if (len == 0) {
			return BCME_BUFTOOSHORT;
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0) < 0) {
			DHD_ERROR(("%s: failed to get fl_prio_map\n", __FUNCTION__));
			return BCME_ERROR;
		}
		*map = iovbuf[0];
		return BCME_OK;
	}
	val = (uint32)map[0];
	len = bcm_mkiovar("bus:fl_prio_map", (char *)&val, sizeof(val),
		(char*)iovbuf, sizeof(iovbuf));
	if (len == 0) {
		return BCME_BUFTOOSHORT;
	}
	if (dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, len, TRUE, 0) < 0) {
		DHD_ERROR(("%s: failed to set fl_prio_map \n",
			__FUNCTION__));
		return BCME_ERROR;
	}
	return BCME_OK;
}

uint32
dhd_active_tx_flowring_bkpq_len(dhd_pub_t *dhd)
{
	unsigned long list_lock_flags;
	dll_t *item, *prev;
	flow_ring_node_t *flow_ring_node;
	dhd_bus_t *bus = dhd->bus;
	uint32 active_tx_flowring_qlen = 0;

	DHD_FLOWRING_LIST_LOCK(bus->dhd->flowring_list_lock, list_lock_flags);

	for (item = dll_tail_p(&bus->flowring_active_list);
			!dll_end(&bus->flowring_active_list, item); item = prev) {

		prev = dll_prev_p(item);

		flow_ring_node = dhd_constlist_to_flowring(item);
		if (flow_ring_node->active) {
			DHD_INFO(("%s :%d\n", __FUNCTION__, flow_ring_node->queue.len));
			active_tx_flowring_qlen += flow_ring_node->queue.len;
		}
	}
	DHD_FLOWRING_LIST_UNLOCK(bus->dhd->flowring_list_lock, list_lock_flags);
	return active_tx_flowring_qlen;
}

#ifdef DHD_AWDL
/**
 * Handle/Intercept awdl peer op IOVAR fired by user
 * buf = NULL means delete all peers in awdl interface
 */
void
dhd_awdl_peer_op(dhd_pub_t *dhdp, uint8 ifindex, void *buf, uint32 buflen)
{
	awdl_peer_op_t	*peer = (awdl_peer_op_t *)buf;
	DHD_TRACE(("%s\n", __FUNCTION__));

	ASSERT(ifindex < DHD_MAX_IFS);
	if (ifindex >= DHD_MAX_IFS)
		return;
	if (!buf) {
		/* Delete all peers in awdl interface */
		if_flow_lkup_t *if_flow_lkup;
		if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;
		if (if_flow_lkup[ifindex].role != WLC_E_IF_ROLE_AWDL) {
			DHD_ERROR(("%s: Iinterface %d is not a awdl peer \n",
				__FUNCTION__, ifindex));
			return;
		}
		dhd_flow_rings_delete(dhdp, ifindex);
		return;
	}
	/* Parse awdl_peer_op info now */
	if (buflen < sizeof(awdl_peer_op_t)) {
		DHD_ERROR(("%s: cannot handle awdl_peer_op add/del\n", __FUNCTION__));
		return;
	}
	/**
	 * Only flowring deletion is handled here
	 * Flowring addition is taken care in dhd_flowid_lookup
	 */
	if (peer->opcode == AWDL_PEER_OP_DEL) {
		dhd_del_sta(dhdp, ifindex, &peer->addr.octet[0]);
		dhd_flow_rings_delete_for_peer(dhdp, ifindex, (char *)&peer->addr.octet[0]);
	} else if (peer->opcode == AWDL_PEER_OP_ADD) {
		dhd_findadd_sta(dhdp, ifindex, &peer->addr.octet[0]);
	}
		return;
}
#endif /* DHD_AWDL */
