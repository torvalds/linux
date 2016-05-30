/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"
#include "qed_sriov.h"

/***************************************************************************
* Structures & Definitions
***************************************************************************/

#define SPQ_HIGH_PRI_RESERVE_DEFAULT    (1)
#define SPQ_BLOCK_SLEEP_LENGTH          (1000)

/***************************************************************************
* Blocking Imp. (BLOCK/EBLOCK mode)
***************************************************************************/
static void qed_spq_blocking_cb(struct qed_hwfn *p_hwfn,
				void *cookie,
				union event_ring_data *data,
				u8 fw_return_code)
{
	struct qed_spq_comp_done *comp_done;

	comp_done = (struct qed_spq_comp_done *)cookie;

	comp_done->done			= 0x1;
	comp_done->fw_return_code	= fw_return_code;

	/* make update visible to waiting thread */
	smp_wmb();
}

static int qed_spq_block(struct qed_hwfn *p_hwfn,
			 struct qed_spq_entry *p_ent,
			 u8 *p_fw_ret)
{
	int sleep_count = SPQ_BLOCK_SLEEP_LENGTH;
	struct qed_spq_comp_done *comp_done;
	int rc;

	comp_done = (struct qed_spq_comp_done *)p_ent->comp_cb.cookie;
	while (sleep_count) {
		/* validate we receive completion update */
		smp_rmb();
		if (comp_done->done == 1) {
			if (p_fw_ret)
				*p_fw_ret = comp_done->fw_return_code;
			return 0;
		}
		usleep_range(5000, 10000);
		sleep_count--;
	}

	DP_INFO(p_hwfn, "Ramrod is stuck, requesting MCP drain\n");
	rc = qed_mcp_drain(p_hwfn, p_hwfn->p_main_ptt);
	if (rc != 0)
		DP_NOTICE(p_hwfn, "MCP drain failed\n");

	/* Retry after drain */
	sleep_count = SPQ_BLOCK_SLEEP_LENGTH;
	while (sleep_count) {
		/* validate we receive completion update */
		smp_rmb();
		if (comp_done->done == 1) {
			if (p_fw_ret)
				*p_fw_ret = comp_done->fw_return_code;
			return 0;
		}
		usleep_range(5000, 10000);
		sleep_count--;
	}

	if (comp_done->done == 1) {
		if (p_fw_ret)
			*p_fw_ret = comp_done->fw_return_code;
		return 0;
	}

	DP_NOTICE(p_hwfn, "Ramrod is stuck, MCP drain failed\n");

	return -EBUSY;
}

/***************************************************************************
* SPQ entries inner API
***************************************************************************/
static int
qed_spq_fill_entry(struct qed_hwfn *p_hwfn,
		   struct qed_spq_entry *p_ent)
{
	p_ent->flags = 0;

	switch (p_ent->comp_mode) {
	case QED_SPQ_MODE_EBLOCK:
	case QED_SPQ_MODE_BLOCK:
		p_ent->comp_cb.function = qed_spq_blocking_cb;
		break;
	case QED_SPQ_MODE_CB:
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown SPQE completion mode %d\n",
			  p_ent->comp_mode);
		return -EINVAL;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_SPQ,
		   "Ramrod header: [CID 0x%08x CMD 0x%02x protocol 0x%02x] Data pointer: [%08x:%08x] Completion Mode: %s\n",
		   p_ent->elem.hdr.cid,
		   p_ent->elem.hdr.cmd_id,
		   p_ent->elem.hdr.protocol_id,
		   p_ent->elem.data_ptr.hi,
		   p_ent->elem.data_ptr.lo,
		   D_TRINE(p_ent->comp_mode, QED_SPQ_MODE_EBLOCK,
			   QED_SPQ_MODE_BLOCK, "MODE_EBLOCK", "MODE_BLOCK",
			   "MODE_CB"));

	return 0;
}

/***************************************************************************
* HSI access
***************************************************************************/
static void qed_spq_hw_initialize(struct qed_hwfn *p_hwfn,
				  struct qed_spq *p_spq)
{
	u16				pq;
	struct qed_cxt_info		cxt_info;
	struct core_conn_context	*p_cxt;
	union qed_qm_pq_params		pq_params;
	int				rc;

	cxt_info.iid = p_spq->cid;

	rc = qed_cxt_get_cid_info(p_hwfn, &cxt_info);

	if (rc < 0) {
		DP_NOTICE(p_hwfn, "Cannot find context info for cid=%d\n",
			  p_spq->cid);
		return;
	}

	p_cxt = cxt_info.p_cxt;

	SET_FIELD(p_cxt->xstorm_ag_context.flags10,
		  XSTORM_CORE_CONN_AG_CTX_DQ_CF_EN, 1);
	SET_FIELD(p_cxt->xstorm_ag_context.flags1,
		  XSTORM_CORE_CONN_AG_CTX_DQ_CF_ACTIVE, 1);
	SET_FIELD(p_cxt->xstorm_ag_context.flags9,
		  XSTORM_CORE_CONN_AG_CTX_CONSOLID_PROD_CF_EN, 1);

	/* QM physical queue */
	memset(&pq_params, 0, sizeof(pq_params));
	pq_params.core.tc = LB_TC;
	pq = qed_get_qm_pq(p_hwfn, PROTOCOLID_CORE, &pq_params);
	p_cxt->xstorm_ag_context.physical_q0 = cpu_to_le16(pq);

	p_cxt->xstorm_st_context.spq_base_lo =
		DMA_LO_LE(p_spq->chain.p_phys_addr);
	p_cxt->xstorm_st_context.spq_base_hi =
		DMA_HI_LE(p_spq->chain.p_phys_addr);

	DMA_REGPAIR_LE(p_cxt->xstorm_st_context.consolid_base_addr,
		       p_hwfn->p_consq->chain.p_phys_addr);
}

static int qed_spq_hw_post(struct qed_hwfn *p_hwfn,
			   struct qed_spq *p_spq,
			   struct qed_spq_entry *p_ent)
{
	struct qed_chain *p_chain = &p_hwfn->p_spq->chain;
	u16 echo = qed_chain_get_prod_idx(p_chain);
	struct slow_path_element	*elem;
	struct core_db_data		db;

	p_ent->elem.hdr.echo	= cpu_to_le16(echo);
	elem = qed_chain_produce(p_chain);
	if (!elem) {
		DP_NOTICE(p_hwfn, "Failed to produce from SPQ chain\n");
		return -EINVAL;
	}

	*elem = p_ent->elem; /* struct assignment */

	/* send a doorbell on the slow hwfn session */
	memset(&db, 0, sizeof(db));
	SET_FIELD(db.params, CORE_DB_DATA_DEST, DB_DEST_XCM);
	SET_FIELD(db.params, CORE_DB_DATA_AGG_CMD, DB_AGG_CMD_SET);
	SET_FIELD(db.params, CORE_DB_DATA_AGG_VAL_SEL,
		  DQ_XCM_CORE_SPQ_PROD_CMD);
	db.agg_flags = DQ_XCM_CORE_DQ_CF_CMD;

	/* validate producer is up to-date */
	rmb();

	db.spq_prod = cpu_to_le16(qed_chain_get_prod_idx(p_chain));

	/* do not reorder */
	barrier();

	DOORBELL(p_hwfn, qed_db_addr(p_spq->cid, DQ_DEMS_LEGACY), *(u32 *)&db);

	/* make sure doorbell is rang */
	mmiowb();

	DP_VERBOSE(p_hwfn, QED_MSG_SPQ,
		   "Doorbelled [0x%08x, CID 0x%08x] with Flags: %02x agg_params: %02x, prod: %04x\n",
		   qed_db_addr(p_spq->cid, DQ_DEMS_LEGACY),
		   p_spq->cid, db.params, db.agg_flags,
		   qed_chain_get_prod_idx(p_chain));

	return 0;
}

/***************************************************************************
* Asynchronous events
***************************************************************************/
static int
qed_async_event_completion(struct qed_hwfn *p_hwfn,
			   struct event_ring_entry *p_eqe)
{
	switch (p_eqe->protocol_id) {
	case PROTOCOLID_COMMON:
		return qed_sriov_eqe_event(p_hwfn,
					   p_eqe->opcode,
					   p_eqe->echo, &p_eqe->data);
	default:
		DP_NOTICE(p_hwfn,
			  "Unknown Async completion for protocol: %d\n",
			  p_eqe->protocol_id);
		return -EINVAL;
	}
}

/***************************************************************************
* EQ API
***************************************************************************/
void qed_eq_prod_update(struct qed_hwfn *p_hwfn,
			u16 prod)
{
	u32 addr = GTT_BAR0_MAP_REG_USDM_RAM +
		   USTORM_EQE_CONS_OFFSET(p_hwfn->rel_pf_id);

	REG_WR16(p_hwfn, addr, prod);

	/* keep prod updates ordered */
	mmiowb();
}

int qed_eq_completion(struct qed_hwfn *p_hwfn,
		      void *cookie)

{
	struct qed_eq *p_eq = cookie;
	struct qed_chain *p_chain = &p_eq->chain;
	int rc = 0;

	/* take a snapshot of the FW consumer */
	u16 fw_cons_idx = le16_to_cpu(*p_eq->p_fw_cons);

	DP_VERBOSE(p_hwfn, QED_MSG_SPQ, "fw_cons_idx %x\n", fw_cons_idx);

	/* Need to guarantee the fw_cons index we use points to a usuable
	 * element (to comply with our chain), so our macros would comply
	 */
	if ((fw_cons_idx & qed_chain_get_usable_per_page(p_chain)) ==
	    qed_chain_get_usable_per_page(p_chain))
		fw_cons_idx += qed_chain_get_unusable_per_page(p_chain);

	/* Complete current segment of eq entries */
	while (fw_cons_idx != qed_chain_get_cons_idx(p_chain)) {
		struct event_ring_entry *p_eqe = qed_chain_consume(p_chain);

		if (!p_eqe) {
			rc = -EINVAL;
			break;
		}

		DP_VERBOSE(p_hwfn, QED_MSG_SPQ,
			   "op %x prot %x res0 %x echo %x fwret %x flags %x\n",
			   p_eqe->opcode,
			   p_eqe->protocol_id,
			   p_eqe->reserved0,
			   le16_to_cpu(p_eqe->echo),
			   p_eqe->fw_return_code,
			   p_eqe->flags);

		if (GET_FIELD(p_eqe->flags, EVENT_RING_ENTRY_ASYNC)) {
			if (qed_async_event_completion(p_hwfn, p_eqe))
				rc = -EINVAL;
		} else if (qed_spq_completion(p_hwfn,
					      p_eqe->echo,
					      p_eqe->fw_return_code,
					      &p_eqe->data)) {
			rc = -EINVAL;
		}

		qed_chain_recycle_consumed(p_chain);
	}

	qed_eq_prod_update(p_hwfn, qed_chain_get_prod_idx(p_chain));

	return rc;
}

struct qed_eq *qed_eq_alloc(struct qed_hwfn *p_hwfn,
			    u16 num_elem)
{
	struct qed_eq *p_eq;

	/* Allocate EQ struct */
	p_eq = kzalloc(sizeof(*p_eq), GFP_KERNEL);
	if (!p_eq) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct qed_eq'\n");
		return NULL;
	}

	/* Allocate and initialize EQ chain*/
	if (qed_chain_alloc(p_hwfn->cdev,
			    QED_CHAIN_USE_TO_PRODUCE,
			    QED_CHAIN_MODE_PBL,
			    num_elem,
			    sizeof(union event_ring_element),
			    &p_eq->chain)) {
		DP_NOTICE(p_hwfn, "Failed to allocate eq chain\n");
		goto eq_allocate_fail;
	}

	/* register EQ completion on the SP SB */
	qed_int_register_cb(p_hwfn,
			    qed_eq_completion,
			    p_eq,
			    &p_eq->eq_sb_index,
			    &p_eq->p_fw_cons);

	return p_eq;

eq_allocate_fail:
	qed_eq_free(p_hwfn, p_eq);
	return NULL;
}

void qed_eq_setup(struct qed_hwfn *p_hwfn,
		  struct qed_eq *p_eq)
{
	qed_chain_reset(&p_eq->chain);
}

void qed_eq_free(struct qed_hwfn *p_hwfn,
		 struct qed_eq *p_eq)
{
	if (!p_eq)
		return;
	qed_chain_free(p_hwfn->cdev, &p_eq->chain);
	kfree(p_eq);
}

/***************************************************************************
* CQE API - manipulate EQ functionality
***************************************************************************/
static int qed_cqe_completion(
	struct qed_hwfn *p_hwfn,
	struct eth_slow_path_rx_cqe *cqe,
	enum protocol_type protocol)
{
	if (IS_VF(p_hwfn->cdev))
		return 0;

	/* @@@tmp - it's possible we'll eventually want to handle some
	 * actual commands that can arrive here, but for now this is only
	 * used to complete the ramrod using the echo value on the cqe
	 */
	return qed_spq_completion(p_hwfn, cqe->echo, 0, NULL);
}

int qed_eth_cqe_completion(struct qed_hwfn *p_hwfn,
			   struct eth_slow_path_rx_cqe *cqe)
{
	int rc;

	rc = qed_cqe_completion(p_hwfn, cqe, PROTOCOLID_ETH);
	if (rc)
		DP_NOTICE(p_hwfn,
			  "Failed to handle RXQ CQE [cmd 0x%02x]\n",
			  cqe->ramrod_cmd_id);

	return rc;
}

/***************************************************************************
* Slow hwfn Queue (spq)
***************************************************************************/
void qed_spq_setup(struct qed_hwfn *p_hwfn)
{
	struct qed_spq		*p_spq	= p_hwfn->p_spq;
	struct qed_spq_entry	*p_virt = NULL;
	dma_addr_t		p_phys	= 0;
	unsigned int		i	= 0;

	INIT_LIST_HEAD(&p_spq->pending);
	INIT_LIST_HEAD(&p_spq->completion_pending);
	INIT_LIST_HEAD(&p_spq->free_pool);
	INIT_LIST_HEAD(&p_spq->unlimited_pending);
	spin_lock_init(&p_spq->lock);

	/* SPQ empty pool */
	p_phys	= p_spq->p_phys + offsetof(struct qed_spq_entry, ramrod);
	p_virt	= p_spq->p_virt;

	for (i = 0; i < p_spq->chain.capacity; i++) {
		DMA_REGPAIR_LE(p_virt->elem.data_ptr, p_phys);

		list_add_tail(&p_virt->list, &p_spq->free_pool);

		p_virt++;
		p_phys += sizeof(struct qed_spq_entry);
	}

	/* Statistics */
	p_spq->normal_count		= 0;
	p_spq->comp_count		= 0;
	p_spq->comp_sent_count		= 0;
	p_spq->unlimited_pending_count	= 0;

	bitmap_zero(p_spq->p_comp_bitmap, SPQ_RING_SIZE);
	p_spq->comp_bitmap_idx = 0;

	/* SPQ cid, cannot fail */
	qed_cxt_acquire_cid(p_hwfn, PROTOCOLID_CORE, &p_spq->cid);
	qed_spq_hw_initialize(p_hwfn, p_spq);

	/* reset the chain itself */
	qed_chain_reset(&p_spq->chain);
}

int qed_spq_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_spq		*p_spq	= NULL;
	dma_addr_t		p_phys	= 0;
	struct qed_spq_entry	*p_virt = NULL;

	/* SPQ struct */
	p_spq =
		kzalloc(sizeof(struct qed_spq), GFP_KERNEL);
	if (!p_spq) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct qed_spq'\n");
		return -ENOMEM;
	}

	/* SPQ ring  */
	if (qed_chain_alloc(p_hwfn->cdev,
			    QED_CHAIN_USE_TO_PRODUCE,
			    QED_CHAIN_MODE_SINGLE,
			    0,   /* N/A when the mode is SINGLE */
			    sizeof(struct slow_path_element),
			    &p_spq->chain)) {
		DP_NOTICE(p_hwfn, "Failed to allocate spq chain\n");
		goto spq_allocate_fail;
	}

	/* allocate and fill the SPQ elements (incl. ramrod data list) */
	p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    p_spq->chain.capacity *
				    sizeof(struct qed_spq_entry),
				    &p_phys,
				    GFP_KERNEL);

	if (!p_virt)
		goto spq_allocate_fail;

	p_spq->p_virt = p_virt;
	p_spq->p_phys = p_phys;
	p_hwfn->p_spq = p_spq;

	return 0;

spq_allocate_fail:
	qed_chain_free(p_hwfn->cdev, &p_spq->chain);
	kfree(p_spq);
	return -ENOMEM;
}

void qed_spq_free(struct qed_hwfn *p_hwfn)
{
	struct qed_spq *p_spq = p_hwfn->p_spq;

	if (!p_spq)
		return;

	if (p_spq->p_virt)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  p_spq->chain.capacity *
				  sizeof(struct qed_spq_entry),
				  p_spq->p_virt,
				  p_spq->p_phys);

	qed_chain_free(p_hwfn->cdev, &p_spq->chain);
	;
	kfree(p_spq);
}

int
qed_spq_get_entry(struct qed_hwfn *p_hwfn,
		  struct qed_spq_entry **pp_ent)
{
	struct qed_spq *p_spq = p_hwfn->p_spq;
	struct qed_spq_entry *p_ent = NULL;
	int rc = 0;

	spin_lock_bh(&p_spq->lock);

	if (list_empty(&p_spq->free_pool)) {
		p_ent = kzalloc(sizeof(*p_ent), GFP_ATOMIC);
		if (!p_ent) {
			rc = -ENOMEM;
			goto out_unlock;
		}
		p_ent->queue = &p_spq->unlimited_pending;
	} else {
		p_ent = list_first_entry(&p_spq->free_pool,
					 struct qed_spq_entry,
					 list);
		list_del(&p_ent->list);
		p_ent->queue = &p_spq->pending;
	}

	*pp_ent = p_ent;

out_unlock:
	spin_unlock_bh(&p_spq->lock);
	return rc;
}

/* Locked variant; Should be called while the SPQ lock is taken */
static void __qed_spq_return_entry(struct qed_hwfn *p_hwfn,
				   struct qed_spq_entry *p_ent)
{
	list_add_tail(&p_ent->list, &p_hwfn->p_spq->free_pool);
}

void qed_spq_return_entry(struct qed_hwfn *p_hwfn,
			  struct qed_spq_entry *p_ent)
{
	spin_lock_bh(&p_hwfn->p_spq->lock);
	__qed_spq_return_entry(p_hwfn, p_ent);
	spin_unlock_bh(&p_hwfn->p_spq->lock);
}

/**
 * @brief qed_spq_add_entry - adds a new entry to the pending
 *        list. Should be used while lock is being held.
 *
 * Addes an entry to the pending list is there is room (en empty
 * element is available in the free_pool), or else places the
 * entry in the unlimited_pending pool.
 *
 * @param p_hwfn
 * @param p_ent
 * @param priority
 *
 * @return int
 */
static int
qed_spq_add_entry(struct qed_hwfn *p_hwfn,
		  struct qed_spq_entry *p_ent,
		  enum spq_priority priority)
{
	struct qed_spq *p_spq = p_hwfn->p_spq;

	if (p_ent->queue == &p_spq->unlimited_pending) {

		if (list_empty(&p_spq->free_pool)) {
			list_add_tail(&p_ent->list, &p_spq->unlimited_pending);
			p_spq->unlimited_pending_count++;

			return 0;
		} else {
			struct qed_spq_entry *p_en2;

			p_en2 = list_first_entry(&p_spq->free_pool,
						 struct qed_spq_entry,
						 list);
			list_del(&p_en2->list);

			/* Copy the ring element physical pointer to the new
			 * entry, since we are about to override the entire ring
			 * entry and don't want to lose the pointer.
			 */
			p_ent->elem.data_ptr = p_en2->elem.data_ptr;

			*p_en2 = *p_ent;

			kfree(p_ent);

			p_ent = p_en2;
		}
	}

	/* entry is to be placed in 'pending' queue */
	switch (priority) {
	case QED_SPQ_PRIORITY_NORMAL:
		list_add_tail(&p_ent->list, &p_spq->pending);
		p_spq->normal_count++;
		break;
	case QED_SPQ_PRIORITY_HIGH:
		list_add(&p_ent->list, &p_spq->pending);
		p_spq->high_count++;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/***************************************************************************
* Accessor
***************************************************************************/
u32 qed_spq_get_cid(struct qed_hwfn *p_hwfn)
{
	if (!p_hwfn->p_spq)
		return 0xffffffff;      /* illegal */
	return p_hwfn->p_spq->cid;
}

/***************************************************************************
* Posting new Ramrods
***************************************************************************/
static int qed_spq_post_list(struct qed_hwfn *p_hwfn,
			     struct list_head *head,
			     u32 keep_reserve)
{
	struct qed_spq *p_spq = p_hwfn->p_spq;
	int rc;

	while (qed_chain_get_elem_left(&p_spq->chain) > keep_reserve &&
	       !list_empty(head)) {
		struct qed_spq_entry *p_ent =
			list_first_entry(head, struct qed_spq_entry, list);
		list_del(&p_ent->list);
		list_add_tail(&p_ent->list, &p_spq->completion_pending);
		p_spq->comp_sent_count++;

		rc = qed_spq_hw_post(p_hwfn, p_spq, p_ent);
		if (rc) {
			list_del(&p_ent->list);
			__qed_spq_return_entry(p_hwfn, p_ent);
			return rc;
		}
	}

	return 0;
}

static int qed_spq_pend_post(struct qed_hwfn *p_hwfn)
{
	struct qed_spq *p_spq = p_hwfn->p_spq;
	struct qed_spq_entry *p_ent = NULL;

	while (!list_empty(&p_spq->free_pool)) {
		if (list_empty(&p_spq->unlimited_pending))
			break;

		p_ent = list_first_entry(&p_spq->unlimited_pending,
					 struct qed_spq_entry,
					 list);
		if (!p_ent)
			return -EINVAL;

		list_del(&p_ent->list);

		qed_spq_add_entry(p_hwfn, p_ent, p_ent->priority);
	}

	return qed_spq_post_list(p_hwfn, &p_spq->pending,
				 SPQ_HIGH_PRI_RESERVE_DEFAULT);
}

int qed_spq_post(struct qed_hwfn *p_hwfn,
		 struct qed_spq_entry *p_ent,
		 u8 *fw_return_code)
{
	int rc = 0;
	struct qed_spq *p_spq = p_hwfn ? p_hwfn->p_spq : NULL;
	bool b_ret_ent = true;

	if (!p_hwfn)
		return -EINVAL;

	if (!p_ent) {
		DP_NOTICE(p_hwfn, "Got a NULL pointer\n");
		return -EINVAL;
	}

	/* Complete the entry */
	rc = qed_spq_fill_entry(p_hwfn, p_ent);

	spin_lock_bh(&p_spq->lock);

	/* Check return value after LOCK is taken for cleaner error flow */
	if (rc)
		goto spq_post_fail;

	/* Add the request to the pending queue */
	rc = qed_spq_add_entry(p_hwfn, p_ent, p_ent->priority);
	if (rc)
		goto spq_post_fail;

	rc = qed_spq_pend_post(p_hwfn);
	if (rc) {
		/* Since it's possible that pending failed for a different
		 * entry [although unlikely], the failed entry was already
		 * dealt with; No need to return it here.
		 */
		b_ret_ent = false;
		goto spq_post_fail;
	}

	spin_unlock_bh(&p_spq->lock);

	if (p_ent->comp_mode == QED_SPQ_MODE_EBLOCK) {
		/* For entries in QED BLOCK mode, the completion code cannot
		 * perform the necessary cleanup - if it did, we couldn't
		 * access p_ent here to see whether it's successful or not.
		 * Thus, after gaining the answer perform the cleanup here.
		 */
		rc = qed_spq_block(p_hwfn, p_ent, fw_return_code);
		if (rc)
			goto spq_post_fail2;

		/* return to pool */
		qed_spq_return_entry(p_hwfn, p_ent);
	}
	return rc;

spq_post_fail2:
	spin_lock_bh(&p_spq->lock);
	list_del(&p_ent->list);
	qed_chain_return_produced(&p_spq->chain);

spq_post_fail:
	/* return to the free pool */
	if (b_ret_ent)
		__qed_spq_return_entry(p_hwfn, p_ent);
	spin_unlock_bh(&p_spq->lock);

	return rc;
}

int qed_spq_completion(struct qed_hwfn *p_hwfn,
		       __le16 echo,
		       u8 fw_return_code,
		       union event_ring_data *p_data)
{
	struct qed_spq		*p_spq;
	struct qed_spq_entry	*p_ent = NULL;
	struct qed_spq_entry	*tmp;
	struct qed_spq_entry	*found = NULL;
	int			rc;

	if (!p_hwfn)
		return -EINVAL;

	p_spq = p_hwfn->p_spq;
	if (!p_spq)
		return -EINVAL;

	spin_lock_bh(&p_spq->lock);
	list_for_each_entry_safe(p_ent, tmp, &p_spq->completion_pending,
				 list) {
		if (p_ent->elem.hdr.echo == echo) {
			u16 pos = le16_to_cpu(echo) % SPQ_RING_SIZE;

			list_del(&p_ent->list);

			/* Avoid overriding of SPQ entries when getting
			 * out-of-order completions, by marking the completions
			 * in a bitmap and increasing the chain consumer only
			 * for the first successive completed entries.
			 */
			bitmap_set(p_spq->p_comp_bitmap, pos, SPQ_RING_SIZE);

			while (test_bit(p_spq->comp_bitmap_idx,
					p_spq->p_comp_bitmap)) {
				bitmap_clear(p_spq->p_comp_bitmap,
					     p_spq->comp_bitmap_idx,
					     SPQ_RING_SIZE);
				p_spq->comp_bitmap_idx++;
				qed_chain_return_produced(&p_spq->chain);
			}

			p_spq->comp_count++;
			found = p_ent;
			break;
		}

		/* This is relatively uncommon - depends on scenarios
		 * which have mutliple per-PF sent ramrods.
		 */
		DP_VERBOSE(p_hwfn, QED_MSG_SPQ,
			   "Got completion for echo %04x - doesn't match echo %04x in completion pending list\n",
			   le16_to_cpu(echo),
			   le16_to_cpu(p_ent->elem.hdr.echo));
	}

	/* Release lock before callback, as callback may post
	 * an additional ramrod.
	 */
	spin_unlock_bh(&p_spq->lock);

	if (!found) {
		DP_NOTICE(p_hwfn,
			  "Failed to find an entry this EQE completes\n");
		return -EEXIST;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_SPQ, "Complete: func %p cookie %p)\n",
		   p_ent->comp_cb.function, p_ent->comp_cb.cookie);
	if (found->comp_cb.function)
		found->comp_cb.function(p_hwfn, found->comp_cb.cookie, p_data,
					fw_return_code);

	if (found->comp_mode != QED_SPQ_MODE_EBLOCK)
		/* EBLOCK is responsible for freeing its own entry */
		qed_spq_return_entry(p_hwfn, found);

	/* Attempt to post pending requests */
	spin_lock_bh(&p_spq->lock);
	rc = qed_spq_pend_post(p_hwfn);
	spin_unlock_bh(&p_spq->lock);

	return rc;
}

struct qed_consq *qed_consq_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_consq *p_consq;

	/* Allocate ConsQ struct */
	p_consq = kzalloc(sizeof(*p_consq), GFP_KERNEL);
	if (!p_consq) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct qed_consq'\n");
		return NULL;
	}

	/* Allocate and initialize EQ chain*/
	if (qed_chain_alloc(p_hwfn->cdev,
			    QED_CHAIN_USE_TO_PRODUCE,
			    QED_CHAIN_MODE_PBL,
			    QED_CHAIN_PAGE_SIZE / 0x80,
			    0x80,
			    &p_consq->chain)) {
		DP_NOTICE(p_hwfn, "Failed to allocate consq chain");
		goto consq_allocate_fail;
	}

	return p_consq;

consq_allocate_fail:
	qed_consq_free(p_hwfn, p_consq);
	return NULL;
}

void qed_consq_setup(struct qed_hwfn *p_hwfn,
		     struct qed_consq *p_consq)
{
	qed_chain_reset(&p_consq->chain);
}

void qed_consq_free(struct qed_hwfn *p_hwfn,
		    struct qed_consq *p_consq)
{
	if (!p_consq)
		return;
	qed_chain_free(p_hwfn->cdev, &p_consq->chain);
	kfree(p_consq);
}
