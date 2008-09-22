/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Functions for EQs, NEQs and interrupts
 *
 *  Authors: Heiko J Schick <schickhj@de.ibm.com>
 *           Khadija Souissi <souissi@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *           Joachim Fenkes <fenkes@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ehca_classes.h"
#include "ehca_irq.h"
#include "ehca_iverbs.h"
#include "ehca_tools.h"
#include "hcp_if.h"
#include "hipz_fns.h"
#include "ipz_pt_fn.h"

#define EQE_COMPLETION_EVENT   EHCA_BMASK_IBM( 1,  1)
#define EQE_CQ_QP_NUMBER       EHCA_BMASK_IBM( 8, 31)
#define EQE_EE_IDENTIFIER      EHCA_BMASK_IBM( 2,  7)
#define EQE_CQ_NUMBER          EHCA_BMASK_IBM( 8, 31)
#define EQE_QP_NUMBER          EHCA_BMASK_IBM( 8, 31)
#define EQE_QP_TOKEN           EHCA_BMASK_IBM(32, 63)
#define EQE_CQ_TOKEN           EHCA_BMASK_IBM(32, 63)

#define NEQE_COMPLETION_EVENT  EHCA_BMASK_IBM( 1,  1)
#define NEQE_EVENT_CODE        EHCA_BMASK_IBM( 2,  7)
#define NEQE_PORT_NUMBER       EHCA_BMASK_IBM( 8, 15)
#define NEQE_PORT_AVAILABILITY EHCA_BMASK_IBM(16, 16)
#define NEQE_DISRUPTIVE        EHCA_BMASK_IBM(16, 16)
#define NEQE_SPECIFIC_EVENT    EHCA_BMASK_IBM(16, 23)

#define ERROR_DATA_LENGTH      EHCA_BMASK_IBM(52, 63)
#define ERROR_DATA_TYPE        EHCA_BMASK_IBM( 0,  7)

static void queue_comp_task(struct ehca_cq *__cq);

static struct ehca_comp_pool *pool;

static inline void comp_event_callback(struct ehca_cq *cq)
{
	if (!cq->ib_cq.comp_handler)
		return;

	spin_lock(&cq->cb_lock);
	cq->ib_cq.comp_handler(&cq->ib_cq, cq->ib_cq.cq_context);
	spin_unlock(&cq->cb_lock);

	return;
}

static void print_error_data(struct ehca_shca *shca, void *data,
			     u64 *rblock, int length)
{
	u64 type = EHCA_BMASK_GET(ERROR_DATA_TYPE, rblock[2]);
	u64 resource = rblock[1];

	switch (type) {
	case 0x1: /* Queue Pair */
	{
		struct ehca_qp *qp = (struct ehca_qp *)data;

		/* only print error data if AER is set */
		if (rblock[6] == 0)
			return;

		ehca_err(&shca->ib_device,
			 "QP 0x%x (resource=%lx) has errors.",
			 qp->ib_qp.qp_num, resource);
		break;
	}
	case 0x4: /* Completion Queue */
	{
		struct ehca_cq *cq = (struct ehca_cq *)data;

		ehca_err(&shca->ib_device,
			 "CQ 0x%x (resource=%lx) has errors.",
			 cq->cq_number, resource);
		break;
	}
	default:
		ehca_err(&shca->ib_device,
			 "Unknown error type: %lx on %s.",
			 type, shca->ib_device.name);
		break;
	}

	ehca_err(&shca->ib_device, "Error data is available: %lx.", resource);
	ehca_err(&shca->ib_device, "EHCA ----- error data begin "
		 "---------------------------------------------------");
	ehca_dmp(rblock, length, "resource=%lx", resource);
	ehca_err(&shca->ib_device, "EHCA ----- error data end "
		 "----------------------------------------------------");

	return;
}

int ehca_error_data(struct ehca_shca *shca, void *data,
		    u64 resource)
{

	unsigned long ret;
	u64 *rblock;
	unsigned long block_count;

	rblock = ehca_alloc_fw_ctrlblock(GFP_ATOMIC);
	if (!rblock) {
		ehca_err(&shca->ib_device, "Cannot allocate rblock memory.");
		ret = -ENOMEM;
		goto error_data1;
	}

	/* rblock must be 4K aligned and should be 4K large */
	ret = hipz_h_error_data(shca->ipz_hca_handle,
				resource,
				rblock,
				&block_count);

	if (ret == H_R_STATE)
		ehca_err(&shca->ib_device,
			 "No error data is available: %lx.", resource);
	else if (ret == H_SUCCESS) {
		int length;

		length = EHCA_BMASK_GET(ERROR_DATA_LENGTH, rblock[0]);

		if (length > EHCA_PAGESIZE)
			length = EHCA_PAGESIZE;

		print_error_data(shca, data, rblock, length);
	} else
		ehca_err(&shca->ib_device,
			 "Error data could not be fetched: %lx", resource);

	ehca_free_fw_ctrlblock(rblock);

error_data1:
	return ret;

}

static void dispatch_qp_event(struct ehca_shca *shca, struct ehca_qp *qp,
			      enum ib_event_type event_type)
{
	struct ib_event event;

	/* PATH_MIG without the QP ever having been armed is false alarm */
	if (event_type == IB_EVENT_PATH_MIG && !qp->mig_armed)
		return;

	event.device = &shca->ib_device;
	event.event = event_type;

	if (qp->ext_type == EQPT_SRQ) {
		if (!qp->ib_srq.event_handler)
			return;

		event.element.srq = &qp->ib_srq;
		qp->ib_srq.event_handler(&event, qp->ib_srq.srq_context);
	} else {
		if (!qp->ib_qp.event_handler)
			return;

		event.element.qp = &qp->ib_qp;
		qp->ib_qp.event_handler(&event, qp->ib_qp.qp_context);
	}
}

static void qp_event_callback(struct ehca_shca *shca, u64 eqe,
			      enum ib_event_type event_type, int fatal)
{
	struct ehca_qp *qp;
	u32 token = EHCA_BMASK_GET(EQE_QP_TOKEN, eqe);

	read_lock(&ehca_qp_idr_lock);
	qp = idr_find(&ehca_qp_idr, token);
	if (qp)
		atomic_inc(&qp->nr_events);
	read_unlock(&ehca_qp_idr_lock);

	if (!qp)
		return;

	if (fatal)
		ehca_error_data(shca, qp, qp->ipz_qp_handle.handle);

	dispatch_qp_event(shca, qp, fatal && qp->ext_type == EQPT_SRQ ?
			  IB_EVENT_SRQ_ERR : event_type);

	/*
	 * eHCA only processes one WQE at a time for SRQ base QPs,
	 * so the last WQE has been processed as soon as the QP enters
	 * error state.
	 */
	if (fatal && qp->ext_type == EQPT_SRQBASE)
		dispatch_qp_event(shca, qp, IB_EVENT_QP_LAST_WQE_REACHED);

	if (atomic_dec_and_test(&qp->nr_events))
		wake_up(&qp->wait_completion);
	return;
}

static void cq_event_callback(struct ehca_shca *shca,
			      u64 eqe)
{
	struct ehca_cq *cq;
	u32 token = EHCA_BMASK_GET(EQE_CQ_TOKEN, eqe);

	read_lock(&ehca_cq_idr_lock);
	cq = idr_find(&ehca_cq_idr, token);
	if (cq)
		atomic_inc(&cq->nr_events);
	read_unlock(&ehca_cq_idr_lock);

	if (!cq)
		return;

	ehca_error_data(shca, cq, cq->ipz_cq_handle.handle);

	if (atomic_dec_and_test(&cq->nr_events))
		wake_up(&cq->wait_completion);

	return;
}

static void parse_identifier(struct ehca_shca *shca, u64 eqe)
{
	u8 identifier = EHCA_BMASK_GET(EQE_EE_IDENTIFIER, eqe);

	switch (identifier) {
	case 0x02: /* path migrated */
		qp_event_callback(shca, eqe, IB_EVENT_PATH_MIG, 0);
		break;
	case 0x03: /* communication established */
		qp_event_callback(shca, eqe, IB_EVENT_COMM_EST, 0);
		break;
	case 0x04: /* send queue drained */
		qp_event_callback(shca, eqe, IB_EVENT_SQ_DRAINED, 0);
		break;
	case 0x05: /* QP error */
	case 0x06: /* QP error */
		qp_event_callback(shca, eqe, IB_EVENT_QP_FATAL, 1);
		break;
	case 0x07: /* CQ error */
	case 0x08: /* CQ error */
		cq_event_callback(shca, eqe);
		break;
	case 0x09: /* MRMWPTE error */
		ehca_err(&shca->ib_device, "MRMWPTE error.");
		break;
	case 0x0A: /* port event */
		ehca_err(&shca->ib_device, "Port event.");
		break;
	case 0x0B: /* MR access error */
		ehca_err(&shca->ib_device, "MR access error.");
		break;
	case 0x0C: /* EQ error */
		ehca_err(&shca->ib_device, "EQ error.");
		break;
	case 0x0D: /* P/Q_Key mismatch */
		ehca_err(&shca->ib_device, "P/Q_Key mismatch.");
		break;
	case 0x10: /* sampling complete */
		ehca_err(&shca->ib_device, "Sampling complete.");
		break;
	case 0x11: /* unaffiliated access error */
		ehca_err(&shca->ib_device, "Unaffiliated access error.");
		break;
	case 0x12: /* path migrating */
		ehca_err(&shca->ib_device, "Path migrating.");
		break;
	case 0x13: /* interface trace stopped */
		ehca_err(&shca->ib_device, "Interface trace stopped.");
		break;
	case 0x14: /* first error capture info available */
		ehca_info(&shca->ib_device, "First error capture available");
		break;
	case 0x15: /* SRQ limit reached */
		qp_event_callback(shca, eqe, IB_EVENT_SRQ_LIMIT_REACHED, 0);
		break;
	default:
		ehca_err(&shca->ib_device, "Unknown identifier: %x on %s.",
			 identifier, shca->ib_device.name);
		break;
	}

	return;
}

static void dispatch_port_event(struct ehca_shca *shca, int port_num,
				enum ib_event_type type, const char *msg)
{
	struct ib_event event;

	ehca_info(&shca->ib_device, "port %d %s.", port_num, msg);
	event.device = &shca->ib_device;
	event.event = type;
	event.element.port_num = port_num;
	ib_dispatch_event(&event);
}

static void notify_port_conf_change(struct ehca_shca *shca, int port_num)
{
	struct ehca_sma_attr  new_attr;
	struct ehca_sma_attr *old_attr = &shca->sport[port_num - 1].saved_attr;

	ehca_query_sma_attr(shca, port_num, &new_attr);

	if (new_attr.sm_sl  != old_attr->sm_sl ||
	    new_attr.sm_lid != old_attr->sm_lid)
		dispatch_port_event(shca, port_num, IB_EVENT_SM_CHANGE,
				    "SM changed");

	if (new_attr.lid != old_attr->lid ||
	    new_attr.lmc != old_attr->lmc)
		dispatch_port_event(shca, port_num, IB_EVENT_LID_CHANGE,
				    "LID changed");

	if (new_attr.pkey_tbl_len != old_attr->pkey_tbl_len ||
	    memcmp(new_attr.pkeys, old_attr->pkeys,
		   sizeof(u16) * new_attr.pkey_tbl_len))
		dispatch_port_event(shca, port_num, IB_EVENT_PKEY_CHANGE,
				    "P_Key changed");

	*old_attr = new_attr;
}

static void parse_ec(struct ehca_shca *shca, u64 eqe)
{
	u8 ec   = EHCA_BMASK_GET(NEQE_EVENT_CODE, eqe);
	u8 port = EHCA_BMASK_GET(NEQE_PORT_NUMBER, eqe);
	u8 spec_event;
	struct ehca_sport *sport = &shca->sport[port - 1];
	unsigned long flags;

	switch (ec) {
	case 0x30: /* port availability change */
		if (EHCA_BMASK_GET(NEQE_PORT_AVAILABILITY, eqe)) {
			int suppress_event;
			/* replay modify_qp for sqps */
			spin_lock_irqsave(&sport->mod_sqp_lock, flags);
			suppress_event = !sport->ibqp_sqp[IB_QPT_GSI];
			if (sport->ibqp_sqp[IB_QPT_SMI])
				ehca_recover_sqp(sport->ibqp_sqp[IB_QPT_SMI]);
			if (!suppress_event)
				ehca_recover_sqp(sport->ibqp_sqp[IB_QPT_GSI]);
			spin_unlock_irqrestore(&sport->mod_sqp_lock, flags);

			/* AQP1 was destroyed, ignore this event */
			if (suppress_event)
				break;

			sport->port_state = IB_PORT_ACTIVE;
			dispatch_port_event(shca, port, IB_EVENT_PORT_ACTIVE,
					    "is active");
			ehca_query_sma_attr(shca, port,
					    &sport->saved_attr);
		} else {
			sport->port_state = IB_PORT_DOWN;
			dispatch_port_event(shca, port, IB_EVENT_PORT_ERR,
					    "is inactive");
		}
		break;
	case 0x31:
		/* port configuration change
		 * disruptive change is caused by
		 * LID, PKEY or SM change
		 */
		if (EHCA_BMASK_GET(NEQE_DISRUPTIVE, eqe)) {
			ehca_warn(&shca->ib_device, "disruptive port "
				  "%d configuration change", port);

			sport->port_state = IB_PORT_DOWN;
			dispatch_port_event(shca, port, IB_EVENT_PORT_ERR,
					    "is inactive");

			sport->port_state = IB_PORT_ACTIVE;
			dispatch_port_event(shca, port, IB_EVENT_PORT_ACTIVE,
					    "is active");
			ehca_query_sma_attr(shca, port,
					    &sport->saved_attr);
		} else
			notify_port_conf_change(shca, port);
		break;
	case 0x32: /* adapter malfunction */
		ehca_err(&shca->ib_device, "Adapter malfunction.");
		break;
	case 0x33:  /* trace stopped */
		ehca_err(&shca->ib_device, "Traced stopped.");
		break;
	case 0x34: /* util async event */
		spec_event = EHCA_BMASK_GET(NEQE_SPECIFIC_EVENT, eqe);
		if (spec_event == 0x80) /* client reregister required */
			dispatch_port_event(shca, port,
					    IB_EVENT_CLIENT_REREGISTER,
					    "client reregister req.");
		else
			ehca_warn(&shca->ib_device, "Unknown util async "
				  "event %x on port %x", spec_event, port);
		break;
	default:
		ehca_err(&shca->ib_device, "Unknown event code: %x on %s.",
			 ec, shca->ib_device.name);
		break;
	}

	return;
}

static inline void reset_eq_pending(struct ehca_cq *cq)
{
	u64 CQx_EP;
	struct h_galpa gal = cq->galpas.kernel;

	hipz_galpa_store_cq(gal, cqx_ep, 0x0);
	CQx_EP = hipz_galpa_load(gal, CQTEMM_OFFSET(cqx_ep));

	return;
}

irqreturn_t ehca_interrupt_neq(int irq, void *dev_id)
{
	struct ehca_shca *shca = (struct ehca_shca*)dev_id;

	tasklet_hi_schedule(&shca->neq.interrupt_task);

	return IRQ_HANDLED;
}

void ehca_tasklet_neq(unsigned long data)
{
	struct ehca_shca *shca = (struct ehca_shca*)data;
	struct ehca_eqe *eqe;
	u64 ret;

	eqe = (struct ehca_eqe *)ehca_poll_eq(shca, &shca->neq);

	while (eqe) {
		if (!EHCA_BMASK_GET(NEQE_COMPLETION_EVENT, eqe->entry))
			parse_ec(shca, eqe->entry);

		eqe = (struct ehca_eqe *)ehca_poll_eq(shca, &shca->neq);
	}

	ret = hipz_h_reset_event(shca->ipz_hca_handle,
				 shca->neq.ipz_eq_handle, 0xFFFFFFFFFFFFFFFFL);

	if (ret != H_SUCCESS)
		ehca_err(&shca->ib_device, "Can't clear notification events.");

	return;
}

irqreturn_t ehca_interrupt_eq(int irq, void *dev_id)
{
	struct ehca_shca *shca = (struct ehca_shca*)dev_id;

	tasklet_hi_schedule(&shca->eq.interrupt_task);

	return IRQ_HANDLED;
}


static inline void process_eqe(struct ehca_shca *shca, struct ehca_eqe *eqe)
{
	u64 eqe_value;
	u32 token;
	struct ehca_cq *cq;

	eqe_value = eqe->entry;
	ehca_dbg(&shca->ib_device, "eqe_value=%lx", eqe_value);
	if (EHCA_BMASK_GET(EQE_COMPLETION_EVENT, eqe_value)) {
		ehca_dbg(&shca->ib_device, "Got completion event");
		token = EHCA_BMASK_GET(EQE_CQ_TOKEN, eqe_value);
		read_lock(&ehca_cq_idr_lock);
		cq = idr_find(&ehca_cq_idr, token);
		if (cq)
			atomic_inc(&cq->nr_events);
		read_unlock(&ehca_cq_idr_lock);
		if (cq == NULL) {
			ehca_err(&shca->ib_device,
				 "Invalid eqe for non-existing cq token=%x",
				 token);
			return;
		}
		reset_eq_pending(cq);
		if (ehca_scaling_code)
			queue_comp_task(cq);
		else {
			comp_event_callback(cq);
			if (atomic_dec_and_test(&cq->nr_events))
				wake_up(&cq->wait_completion);
		}
	} else {
		ehca_dbg(&shca->ib_device, "Got non completion event");
		parse_identifier(shca, eqe_value);
	}
}

void ehca_process_eq(struct ehca_shca *shca, int is_irq)
{
	struct ehca_eq *eq = &shca->eq;
	struct ehca_eqe_cache_entry *eqe_cache = eq->eqe_cache;
	u64 eqe_value, ret;
	unsigned long flags;
	int eqe_cnt, i;
	int eq_empty = 0;

	spin_lock_irqsave(&eq->irq_spinlock, flags);
	if (is_irq) {
		const int max_query_cnt = 100;
		int query_cnt = 0;
		int int_state = 1;
		do {
			int_state = hipz_h_query_int_state(
				shca->ipz_hca_handle, eq->ist);
			query_cnt++;
			iosync();
		} while (int_state && query_cnt < max_query_cnt);
		if (unlikely((query_cnt == max_query_cnt)))
			ehca_dbg(&shca->ib_device, "int_state=%x query_cnt=%x",
				 int_state, query_cnt);
	}

	/* read out all eqes */
	eqe_cnt = 0;
	do {
		u32 token;
		eqe_cache[eqe_cnt].eqe =
			(struct ehca_eqe *)ehca_poll_eq(shca, eq);
		if (!eqe_cache[eqe_cnt].eqe)
			break;
		eqe_value = eqe_cache[eqe_cnt].eqe->entry;
		if (EHCA_BMASK_GET(EQE_COMPLETION_EVENT, eqe_value)) {
			token = EHCA_BMASK_GET(EQE_CQ_TOKEN, eqe_value);
			read_lock(&ehca_cq_idr_lock);
			eqe_cache[eqe_cnt].cq = idr_find(&ehca_cq_idr, token);
			if (eqe_cache[eqe_cnt].cq)
				atomic_inc(&eqe_cache[eqe_cnt].cq->nr_events);
			read_unlock(&ehca_cq_idr_lock);
			if (!eqe_cache[eqe_cnt].cq) {
				ehca_err(&shca->ib_device,
					 "Invalid eqe for non-existing cq "
					 "token=%x", token);
				continue;
			}
		} else
			eqe_cache[eqe_cnt].cq = NULL;
		eqe_cnt++;
	} while (eqe_cnt < EHCA_EQE_CACHE_SIZE);
	if (!eqe_cnt) {
		if (is_irq)
			ehca_dbg(&shca->ib_device,
				 "No eqe found for irq event");
		goto unlock_irq_spinlock;
	} else if (!is_irq) {
		ret = hipz_h_eoi(eq->ist);
		if (ret != H_SUCCESS)
			ehca_err(&shca->ib_device,
				 "bad return code EOI -rc = %ld\n", ret);
		ehca_dbg(&shca->ib_device, "deadman found %x eqe", eqe_cnt);
	}
	if (unlikely(eqe_cnt == EHCA_EQE_CACHE_SIZE))
		ehca_dbg(&shca->ib_device, "too many eqes for one irq event");
	/* enable irq for new packets */
	for (i = 0; i < eqe_cnt; i++) {
		if (eq->eqe_cache[i].cq)
			reset_eq_pending(eq->eqe_cache[i].cq);
	}
	/* check eq */
	spin_lock(&eq->spinlock);
	eq_empty = (!ipz_eqit_eq_peek_valid(&shca->eq.ipz_queue));
	spin_unlock(&eq->spinlock);
	/* call completion handler for cached eqes */
	for (i = 0; i < eqe_cnt; i++)
		if (eq->eqe_cache[i].cq) {
			if (ehca_scaling_code)
				queue_comp_task(eq->eqe_cache[i].cq);
			else {
				struct ehca_cq *cq = eq->eqe_cache[i].cq;
				comp_event_callback(cq);
				if (atomic_dec_and_test(&cq->nr_events))
					wake_up(&cq->wait_completion);
			}
		} else {
			ehca_dbg(&shca->ib_device, "Got non completion event");
			parse_identifier(shca, eq->eqe_cache[i].eqe->entry);
		}
	/* poll eq if not empty */
	if (eq_empty)
		goto unlock_irq_spinlock;
	do {
		struct ehca_eqe *eqe;
		eqe = (struct ehca_eqe *)ehca_poll_eq(shca, &shca->eq);
		if (!eqe)
			break;
		process_eqe(shca, eqe);
	} while (1);

unlock_irq_spinlock:
	spin_unlock_irqrestore(&eq->irq_spinlock, flags);
}

void ehca_tasklet_eq(unsigned long data)
{
	ehca_process_eq((struct ehca_shca*)data, 1);
}

static inline int find_next_online_cpu(struct ehca_comp_pool *pool)
{
	int cpu;
	unsigned long flags;

	WARN_ON_ONCE(!in_interrupt());
	if (ehca_debug_level >= 3)
		ehca_dmp(&cpu_online_map, sizeof(cpumask_t), "");

	spin_lock_irqsave(&pool->last_cpu_lock, flags);
	cpu = next_cpu_nr(pool->last_cpu, cpu_online_map);
	if (cpu >= nr_cpu_ids)
		cpu = first_cpu(cpu_online_map);
	pool->last_cpu = cpu;
	spin_unlock_irqrestore(&pool->last_cpu_lock, flags);

	return cpu;
}

static void __queue_comp_task(struct ehca_cq *__cq,
			      struct ehca_cpu_comp_task *cct)
{
	unsigned long flags;

	spin_lock_irqsave(&cct->task_lock, flags);
	spin_lock(&__cq->task_lock);

	if (__cq->nr_callbacks == 0) {
		__cq->nr_callbacks++;
		list_add_tail(&__cq->entry, &cct->cq_list);
		cct->cq_jobs++;
		wake_up(&cct->wait_queue);
	} else
		__cq->nr_callbacks++;

	spin_unlock(&__cq->task_lock);
	spin_unlock_irqrestore(&cct->task_lock, flags);
}

static void queue_comp_task(struct ehca_cq *__cq)
{
	int cpu_id;
	struct ehca_cpu_comp_task *cct;
	int cq_jobs;
	unsigned long flags;

	cpu_id = find_next_online_cpu(pool);
	BUG_ON(!cpu_online(cpu_id));

	cct = per_cpu_ptr(pool->cpu_comp_tasks, cpu_id);
	BUG_ON(!cct);

	spin_lock_irqsave(&cct->task_lock, flags);
	cq_jobs = cct->cq_jobs;
	spin_unlock_irqrestore(&cct->task_lock, flags);
	if (cq_jobs > 0) {
		cpu_id = find_next_online_cpu(pool);
		cct = per_cpu_ptr(pool->cpu_comp_tasks, cpu_id);
		BUG_ON(!cct);
	}

	__queue_comp_task(__cq, cct);
}

static void run_comp_task(struct ehca_cpu_comp_task *cct)
{
	struct ehca_cq *cq;
	unsigned long flags;

	spin_lock_irqsave(&cct->task_lock, flags);

	while (!list_empty(&cct->cq_list)) {
		cq = list_entry(cct->cq_list.next, struct ehca_cq, entry);
		spin_unlock_irqrestore(&cct->task_lock, flags);

		comp_event_callback(cq);
		if (atomic_dec_and_test(&cq->nr_events))
			wake_up(&cq->wait_completion);

		spin_lock_irqsave(&cct->task_lock, flags);
		spin_lock(&cq->task_lock);
		cq->nr_callbacks--;
		if (!cq->nr_callbacks) {
			list_del_init(cct->cq_list.next);
			cct->cq_jobs--;
		}
		spin_unlock(&cq->task_lock);
	}

	spin_unlock_irqrestore(&cct->task_lock, flags);
}

static int comp_task(void *__cct)
{
	struct ehca_cpu_comp_task *cct = __cct;
	int cql_empty;
	DECLARE_WAITQUEUE(wait, current);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		add_wait_queue(&cct->wait_queue, &wait);

		spin_lock_irq(&cct->task_lock);
		cql_empty = list_empty(&cct->cq_list);
		spin_unlock_irq(&cct->task_lock);
		if (cql_empty)
			schedule();
		else
			__set_current_state(TASK_RUNNING);

		remove_wait_queue(&cct->wait_queue, &wait);

		spin_lock_irq(&cct->task_lock);
		cql_empty = list_empty(&cct->cq_list);
		spin_unlock_irq(&cct->task_lock);
		if (!cql_empty)
			run_comp_task(__cct);

		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);

	return 0;
}

static struct task_struct *create_comp_task(struct ehca_comp_pool *pool,
					    int cpu)
{
	struct ehca_cpu_comp_task *cct;

	cct = per_cpu_ptr(pool->cpu_comp_tasks, cpu);
	spin_lock_init(&cct->task_lock);
	INIT_LIST_HEAD(&cct->cq_list);
	init_waitqueue_head(&cct->wait_queue);
	cct->task = kthread_create(comp_task, cct, "ehca_comp/%d", cpu);

	return cct->task;
}

static void destroy_comp_task(struct ehca_comp_pool *pool,
			      int cpu)
{
	struct ehca_cpu_comp_task *cct;
	struct task_struct *task;
	unsigned long flags_cct;

	cct = per_cpu_ptr(pool->cpu_comp_tasks, cpu);

	spin_lock_irqsave(&cct->task_lock, flags_cct);

	task = cct->task;
	cct->task = NULL;
	cct->cq_jobs = 0;

	spin_unlock_irqrestore(&cct->task_lock, flags_cct);

	if (task)
		kthread_stop(task);
}

static void __cpuinit take_over_work(struct ehca_comp_pool *pool, int cpu)
{
	struct ehca_cpu_comp_task *cct = per_cpu_ptr(pool->cpu_comp_tasks, cpu);
	LIST_HEAD(list);
	struct ehca_cq *cq;
	unsigned long flags_cct;

	spin_lock_irqsave(&cct->task_lock, flags_cct);

	list_splice_init(&cct->cq_list, &list);

	while (!list_empty(&list)) {
		cq = list_entry(cct->cq_list.next, struct ehca_cq, entry);

		list_del(&cq->entry);
		__queue_comp_task(cq, per_cpu_ptr(pool->cpu_comp_tasks,
						  smp_processor_id()));
	}

	spin_unlock_irqrestore(&cct->task_lock, flags_cct);

}

static int __cpuinit comp_pool_callback(struct notifier_block *nfb,
					unsigned long action,
					void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct ehca_cpu_comp_task *cct;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		ehca_gen_dbg("CPU: %x (CPU_PREPARE)", cpu);
		if (!create_comp_task(pool, cpu)) {
			ehca_gen_err("Can't create comp_task for cpu: %x", cpu);
			return NOTIFY_BAD;
		}
		break;
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		ehca_gen_dbg("CPU: %x (CPU_CANCELED)", cpu);
		cct = per_cpu_ptr(pool->cpu_comp_tasks, cpu);
		kthread_bind(cct->task, any_online_cpu(cpu_online_map));
		destroy_comp_task(pool, cpu);
		break;
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		ehca_gen_dbg("CPU: %x (CPU_ONLINE)", cpu);
		cct = per_cpu_ptr(pool->cpu_comp_tasks, cpu);
		kthread_bind(cct->task, cpu);
		wake_up_process(cct->task);
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		ehca_gen_dbg("CPU: %x (CPU_DOWN_PREPARE)", cpu);
		break;
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		ehca_gen_dbg("CPU: %x (CPU_DOWN_FAILED)", cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		ehca_gen_dbg("CPU: %x (CPU_DEAD)", cpu);
		destroy_comp_task(pool, cpu);
		take_over_work(pool, cpu);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block comp_pool_callback_nb __cpuinitdata = {
	.notifier_call	= comp_pool_callback,
	.priority	= 0,
};

int ehca_create_comp_pool(void)
{
	int cpu;
	struct task_struct *task;

	if (!ehca_scaling_code)
		return 0;

	pool = kzalloc(sizeof(struct ehca_comp_pool), GFP_KERNEL);
	if (pool == NULL)
		return -ENOMEM;

	spin_lock_init(&pool->last_cpu_lock);
	pool->last_cpu = any_online_cpu(cpu_online_map);

	pool->cpu_comp_tasks = alloc_percpu(struct ehca_cpu_comp_task);
	if (pool->cpu_comp_tasks == NULL) {
		kfree(pool);
		return -EINVAL;
	}

	for_each_online_cpu(cpu) {
		task = create_comp_task(pool, cpu);
		if (task) {
			kthread_bind(task, cpu);
			wake_up_process(task);
		}
	}

	register_hotcpu_notifier(&comp_pool_callback_nb);

	printk(KERN_INFO "eHCA scaling code enabled\n");

	return 0;
}

void ehca_destroy_comp_pool(void)
{
	int i;

	if (!ehca_scaling_code)
		return;

	unregister_hotcpu_notifier(&comp_pool_callback_nb);

	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_online(i))
			destroy_comp_task(pool, i);
	}
	free_percpu(pool->cpu_comp_tasks);
	kfree(pool);
}
