// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#include <linux/workqueue.h>
#include "fnic.h"
#include "fdls_fc.h"
#include "fnic_fdls.h"
#include <scsi/fc/fc_fcp.h>
#include <scsi/scsi_transport_fc.h>
#include <linux/utsname.h>

#define FC_FC4_TYPE_SCSI 0x08
#define PORT_SPEED_BIT_8 8
#define PORT_SPEED_BIT_9 9
#define PORT_SPEED_BIT_14 14
#define PORT_SPEED_BIT_15 15

#define RETRIES_EXHAUSTED(iport)      \
	(iport->fabric.retry_counter == FABRIC_LOGO_MAX_RETRY)

#define FNIC_TPORT_MAX_NEXUS_RESTART (8)

#define SCHEDULE_OXID_FREE_RETRY_TIME (300)

/* Private Functions */
static void fdls_send_rpn_id(struct fnic_iport_s *iport);
static void fdls_process_flogi_rsp(struct fnic_iport_s *iport,
				   struct fc_frame_header *fchdr,
				   void *rx_frame);
static void fnic_fdls_start_plogi(struct fnic_iport_s *iport);
static void fnic_fdls_start_flogi(struct fnic_iport_s *iport);
static void fdls_start_fabric_timer(struct fnic_iport_s *iport,
			int timeout);
static void fdls_init_plogi_frame(uint8_t *frame, struct fnic_iport_s *iport);
static void fdls_init_logo_frame(uint8_t *frame, struct fnic_iport_s *iport);
static void fdls_init_fabric_abts_frame(uint8_t *frame,
						struct fnic_iport_s *iport);

uint8_t *fdls_alloc_frame(struct fnic_iport_s *iport)
{
	struct fnic *fnic = iport->fnic;
	uint8_t *frame = NULL;

	frame = mempool_alloc(fnic->frame_pool, GFP_ATOMIC);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
				"Failed to allocate frame");
		return NULL;
	}

	memset(frame, 0, FNIC_FCOE_FRAME_MAXSZ);
	return frame;
}

/**
 * fdls_alloc_oxid - Allocate an oxid from the bitmap based oxid pool
 * @iport: Handle to iport instance
 * @oxid_frame_type: Type of frame to allocate
 * @active_oxid: the oxid which is in use
 *
 * Called with fnic lock held
 */
uint16_t fdls_alloc_oxid(struct fnic_iport_s *iport, int oxid_frame_type,
	uint16_t *active_oxid)
{
	struct fnic *fnic = iport->fnic;
	struct fnic_oxid_pool_s *oxid_pool = &iport->oxid_pool;
	int idx;
	uint16_t oxid;

	lockdep_assert_held(&fnic->fnic_lock);

	/*
	 * Allocate next available oxid from bitmap
	 */
	idx = find_next_zero_bit(oxid_pool->bitmap, FNIC_OXID_POOL_SZ, oxid_pool->next_idx);
	if (idx == FNIC_OXID_POOL_SZ) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Alloc oxid: all oxid slots are busy iport state:%d\n",
			iport->state);
		return FNIC_UNASSIGNED_OXID;
	}

	WARN_ON(test_and_set_bit(idx, oxid_pool->bitmap));
	oxid_pool->next_idx = (idx + 1) % FNIC_OXID_POOL_SZ;	/* cycle through the bitmap */

	oxid = FNIC_OXID_ENCODE(idx, oxid_frame_type);
	*active_oxid = oxid;

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
	   "alloc oxid: 0x%x, iport state: %d\n",
	   oxid, iport->state);
	return oxid;
}

/**
 * fdls_free_oxid_idx - Free the oxid using the idx
 * @iport: Handle to iport instance
 * @oxid_idx: The index to free
 *
 * Free the oxid immediately and make it available for new requests
 * Called with fnic lock held
 */
static void fdls_free_oxid_idx(struct fnic_iport_s *iport, uint16_t oxid_idx)
{
	struct fnic *fnic = iport->fnic;
	struct fnic_oxid_pool_s *oxid_pool = &iport->oxid_pool;

	lockdep_assert_held(&fnic->fnic_lock);

		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		"free oxid idx: 0x%x\n", oxid_idx);

	WARN_ON(!test_and_clear_bit(oxid_idx, oxid_pool->bitmap));
}

/**
 * fdls_reclaim_oxid_handler - Callback handler for delayed_oxid_work
 * @work: Handle to work_struct
 *
 * Scheduled when an oxid is to be freed later
 * After freeing expired oxid(s), the handler schedules
 * another callback with the remaining time
 * of next unexpired entry in the reclaim list.
 */
void fdls_reclaim_oxid_handler(struct work_struct *work)
{
	struct fnic_oxid_pool_s *oxid_pool = container_of(work,
		struct fnic_oxid_pool_s, oxid_reclaim_work.work);
	struct fnic_iport_s *iport = container_of(oxid_pool,
		struct fnic_iport_s, oxid_pool);
	struct fnic *fnic = iport->fnic;
	struct reclaim_entry_s *reclaim_entry, *next;
	unsigned long delay_j, cur_jiffies;

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		"Reclaim oxid callback\n");

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	/* Though the work was scheduled for one entry,
	 * walk through and free the expired entries which might have been scheduled
	 * at around the same time as the first entry
	 */
	list_for_each_entry_safe(reclaim_entry, next,
		&(oxid_pool->oxid_reclaim_list), links) {

		/* The list is always maintained in the order of expiry time */
		cur_jiffies = jiffies;
		if (time_before(cur_jiffies, reclaim_entry->expires))
			break;

		list_del(&reclaim_entry->links);
		fdls_free_oxid_idx(iport, reclaim_entry->oxid_idx);
		kfree(reclaim_entry);
	}

	/* schedule to free up the next entry */
	if (!list_empty(&oxid_pool->oxid_reclaim_list)) {
		reclaim_entry = list_first_entry(&oxid_pool->oxid_reclaim_list,
			struct reclaim_entry_s, links);

		delay_j = reclaim_entry->expires - cur_jiffies;
		schedule_delayed_work(&oxid_pool->oxid_reclaim_work, delay_j);
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Scheduling next callback at:%ld jiffies\n", delay_j);
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

/**
 * fdls_free_oxid - Helper function to free the oxid
 * @iport: Handle to iport instance
 * @oxid: oxid to free
 * @active_oxid: the oxid which is in use
 *
 * Called with fnic lock held
 */
void fdls_free_oxid(struct fnic_iport_s *iport,
		uint16_t oxid, uint16_t *active_oxid)
{
	fdls_free_oxid_idx(iport, FNIC_OXID_IDX(oxid));
	*active_oxid = FNIC_UNASSIGNED_OXID;
}

/**
 * fdls_schedule_oxid_free - Schedule oxid to be freed later
 * @iport: Handle to iport instance
 * @active_oxid: the oxid which is in use
 *
 * Gets called in a rare case scenario when both a command
 * (fdls or target discovery) timed out and the following ABTS
 * timed out as well, without a link change.
 *
 * Called with fnic lock held
 */
void fdls_schedule_oxid_free(struct fnic_iport_s *iport, uint16_t *active_oxid)
{
	struct fnic *fnic = iport->fnic;
	struct fnic_oxid_pool_s *oxid_pool = &iport->oxid_pool;
	struct reclaim_entry_s *reclaim_entry;
	unsigned long delay_j = msecs_to_jiffies(OXID_RECLAIM_TOV(iport));
	int oxid_idx = FNIC_OXID_IDX(*active_oxid);

	lockdep_assert_held(&fnic->fnic_lock);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		"Schedule oxid free. oxid: 0x%x\n", *active_oxid);

	*active_oxid = FNIC_UNASSIGNED_OXID;

	reclaim_entry = (struct reclaim_entry_s *)
		kzalloc(sizeof(struct reclaim_entry_s), GFP_ATOMIC);

	if (!reclaim_entry) {
		FNIC_FCS_DBG(KERN_WARNING, fnic->lport->host, fnic->fnic_num,
			"Failed to allocate memory for reclaim struct for oxid idx: %d\n",
			oxid_idx);

		/* Retry the scheduling  */
		WARN_ON(test_and_set_bit(oxid_idx, oxid_pool->pending_schedule_free));
		schedule_delayed_work(&oxid_pool->schedule_oxid_free_retry, 0);
		return;
	}

	reclaim_entry->oxid_idx = oxid_idx;
	reclaim_entry->expires = round_jiffies(jiffies + delay_j);

	list_add_tail(&reclaim_entry->links, &oxid_pool->oxid_reclaim_list);

	schedule_delayed_work(&oxid_pool->oxid_reclaim_work, delay_j);
}

/**
 * fdls_schedule_oxid_free_retry_work - Thread to schedule the
 * oxid to be freed later
 *
 * @work: Handle to the work struct
 */
void fdls_schedule_oxid_free_retry_work(struct work_struct *work)
{
	struct fnic_oxid_pool_s *oxid_pool = container_of(work,
		struct fnic_oxid_pool_s, schedule_oxid_free_retry.work);
	struct fnic_iport_s *iport = container_of(oxid_pool,
		struct fnic_iport_s, oxid_pool);
	struct fnic *fnic = iport->fnic;
	struct reclaim_entry_s *reclaim_entry;
	unsigned long delay_j = msecs_to_jiffies(OXID_RECLAIM_TOV(iport));
	int idx;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	for_each_set_bit(idx, oxid_pool->pending_schedule_free, FNIC_OXID_POOL_SZ) {

		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Schedule oxid free. oxid idx: %d\n", idx);

		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	reclaim_entry = (struct reclaim_entry_s *)
	kzalloc(sizeof(struct reclaim_entry_s), GFP_KERNEL);
		spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

		if (!reclaim_entry) {
			FNIC_FCS_DBG(KERN_WARNING, fnic->lport->host, fnic->fnic_num,
				"Failed to allocate memory for reclaim struct for oxid idx: 0x%x\n",
				idx);

			schedule_delayed_work(&oxid_pool->schedule_oxid_free_retry,
				msecs_to_jiffies(SCHEDULE_OXID_FREE_RETRY_TIME));
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			return;
		}

		if (test_and_clear_bit(idx, oxid_pool->pending_schedule_free)) {
			reclaim_entry->oxid_idx = idx;
			reclaim_entry->expires = round_jiffies(jiffies + delay_j);
			list_add_tail(&reclaim_entry->links, &oxid_pool->oxid_reclaim_list);
			schedule_delayed_work(&oxid_pool->oxid_reclaim_work, delay_j);
		} else {
			/* unlikely scenario, free the allocated memory and continue */
			kfree(reclaim_entry);
		}
}

	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

static bool fdls_is_oxid_fabric_req(uint16_t oxid)
{
	int oxid_frame_type = FNIC_FRAME_TYPE(oxid);

	switch (oxid_frame_type) {
	case FNIC_FRAME_TYPE_FABRIC_FLOGI:
	case FNIC_FRAME_TYPE_FABRIC_PLOGI:
	case FNIC_FRAME_TYPE_FABRIC_RPN:
	case FNIC_FRAME_TYPE_FABRIC_RFT:
	case FNIC_FRAME_TYPE_FABRIC_RFF:
	case FNIC_FRAME_TYPE_FABRIC_GPN_FT:
	case FNIC_FRAME_TYPE_FABRIC_LOGO:
		break;
	default:
		return false;
	}
	return true;
}

static bool fdls_is_oxid_fdmi_req(uint16_t oxid)
{
	int oxid_frame_type = FNIC_FRAME_TYPE(oxid);

	switch (oxid_frame_type) {
	case FNIC_FRAME_TYPE_FDMI_PLOGI:
	case FNIC_FRAME_TYPE_FDMI_RHBA:
	case FNIC_FRAME_TYPE_FDMI_RPA:
		break;
	default:
		return false;
	}
	return true;
}

static bool fdls_is_oxid_tgt_req(uint16_t oxid)
{
	int oxid_frame_type = FNIC_FRAME_TYPE(oxid);

	switch (oxid_frame_type) {
	case FNIC_FRAME_TYPE_TGT_PLOGI:
	case FNIC_FRAME_TYPE_TGT_PRLI:
	case FNIC_FRAME_TYPE_TGT_ADISC:
	case FNIC_FRAME_TYPE_TGT_LOGO:
		break;
	default:
		return false;
	}
	return true;
}

void fnic_del_fabric_timer_sync(struct fnic *fnic)
{
	fnic->iport.fabric.del_timer_inprogress = 1;
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	del_timer_sync(&fnic->iport.fabric.retry_timer);
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	fnic->iport.fabric.del_timer_inprogress = 0;
}

void fnic_del_tport_timer_sync(struct fnic *fnic,
						struct fnic_tport_s *tport)
{
	tport->del_timer_inprogress = 1;
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	del_timer_sync(&tport->retry_timer);
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	tport->del_timer_inprogress = 0;
}

static void
fdls_start_fabric_timer(struct fnic_iport_s *iport, int timeout)
{
	u64 fabric_tov;
	struct fnic *fnic = iport->fnic;

	if (iport->fabric.timer_pending) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "iport fcid: 0x%x: Canceling fabric disc timer\n",
					 iport->fcid);
		fnic_del_fabric_timer_sync(fnic);
		iport->fabric.timer_pending = 0;
	}

	if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED))
		iport->fabric.retry_counter++;

	fabric_tov = jiffies + msecs_to_jiffies(timeout);
	mod_timer(&iport->fabric.retry_timer, round_jiffies(fabric_tov));
	iport->fabric.timer_pending = 1;
	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "fabric timer is %d ", timeout);
}

void fdls_init_plogi_frame(uint8_t *frame,
		struct fnic_iport_s *iport)
{
	struct fc_std_flogi *pplogi;
	uint8_t s_id[3];

	pplogi = (struct fc_std_flogi *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*pplogi = (struct fc_std_flogi) {
		.fchdr = {.fh_r_ctl = FC_RCTL_ELS_REQ, .fh_d_id = {0xFF, 0xFF, 0xFC},
		      .fh_type = FC_TYPE_ELS, .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
		      .fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)},
		.els = {
		    .fl_cmd = ELS_PLOGI,
		    .fl_csp = {.sp_hi_ver = FNIC_FC_PH_VER_HI,
			       .sp_lo_ver = FNIC_FC_PH_VER_LO,
			       .sp_bb_cred = cpu_to_be16(FNIC_FC_B2B_CREDIT),
			       .sp_features = cpu_to_be16(FC_SP_FT_CIRO),
			       .sp_bb_data = cpu_to_be16(FNIC_FC_B2B_RDF_SZ),
			       .sp_tot_seq = cpu_to_be16(FNIC_FC_CONCUR_SEQS),
			       .sp_rel_off = cpu_to_be16(FNIC_FC_RO_INFO),
			       .sp_e_d_tov = cpu_to_be32(FC_DEF_E_D_TOV)},
		    .fl_cssp[2].cp_class = cpu_to_be16(FC_CPC_VALID | FC_CPC_SEQ),
		    .fl_cssp[2].cp_rdfs = cpu_to_be16(0x800),
		    .fl_cssp[2].cp_con_seq = cpu_to_be16(0xFF),
		    .fl_cssp[2].cp_open_seq = 1}
	};

	FNIC_STD_SET_NPORT_NAME(&pplogi->els.fl_wwpn, iport->wwpn);
	FNIC_STD_SET_NODE_NAME(&pplogi->els.fl_wwnn, iport->wwnn);
	FNIC_LOGI_SET_RDF_SIZE(pplogi->els, iport->max_payload_size);

	hton24(s_id, iport->fcid);
	FNIC_STD_SET_S_ID(pplogi->fchdr, s_id);
}

static void fdls_init_logo_frame(uint8_t *frame,
		struct fnic_iport_s *iport)
{
	struct fc_std_logo *plogo;
	uint8_t s_id[3];

	plogo = (struct fc_std_logo *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*plogo = (struct fc_std_logo) {
		.fchdr = {.fh_r_ctl = FC_RCTL_ELS_REQ, .fh_type = FC_TYPE_ELS,
		      .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0}},
		.els.fl_cmd = ELS_LOGO,
	};

	hton24(s_id, iport->fcid);
	FNIC_STD_SET_S_ID(plogo->fchdr, s_id);
	memcpy(plogo->els.fl_n_port_id, s_id, 3);

	FNIC_STD_SET_NPORT_NAME(&plogo->els.fl_n_port_wwn,
			    iport->wwpn);
}

static void fdls_init_fabric_abts_frame(uint8_t *frame,
		struct fnic_iport_s *iport)
{
	struct fc_frame_header *pfabric_abts;

	pfabric_abts = (struct fc_frame_header *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*pfabric_abts = (struct fc_frame_header) {
		.fh_r_ctl = FC_RCTL_BA_ABTS,	/* ABTS */
		.fh_s_id = {0x00, 0x00, 0x00},
		.fh_cs_ctl = 0x00, .fh_type = FC_TYPE_BLS,
		.fh_f_ctl = {FNIC_REQ_ABTS_FCTL, 0, 0}, .fh_seq_id = 0x00,
		.fh_df_ctl = 0x00, .fh_seq_cnt = 0x0000,
		.fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID),
		.fh_parm_offset = 0x00000000,	/* bit:0 = 0 Abort a exchange */
	};
}
static void fdls_send_fabric_abts(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	uint8_t s_id[3];
	uint8_t d_id[3];
	struct fnic *fnic = iport->fnic;
	struct fc_frame_header *pfabric_abts;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_frame_header);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
				"Failed to allocate frame to send fabric ABTS");
		return;
	}

	pfabric_abts = (struct fc_frame_header *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_fabric_abts_frame(frame, iport);

	hton24(s_id, iport->fcid);

	switch (iport->fabric.state) {
	case FDLS_STATE_FABRIC_LOGO:
		hton24(d_id, FC_FID_FLOGI);
		FNIC_STD_SET_D_ID(*pfabric_abts, d_id);
		break;

	case FDLS_STATE_FABRIC_FLOGI:
		hton24(d_id, FC_FID_FLOGI);
		FNIC_STD_SET_D_ID(*pfabric_abts, d_id);
		break;

	case FDLS_STATE_FABRIC_PLOGI:
		FNIC_STD_SET_S_ID(*pfabric_abts, s_id);
		hton24(d_id, FC_FID_DIR_SERV);
		FNIC_STD_SET_D_ID(*pfabric_abts, d_id);
		break;

	case FDLS_STATE_RPN_ID:
		FNIC_STD_SET_S_ID(*pfabric_abts, s_id);
		hton24(d_id, FC_FID_DIR_SERV);
		FNIC_STD_SET_D_ID(*pfabric_abts, d_id);
		break;

	case FDLS_STATE_SCR:
		FNIC_STD_SET_S_ID(*pfabric_abts, s_id);
		hton24(d_id, FC_FID_FCTRL);
		FNIC_STD_SET_D_ID(*pfabric_abts, d_id);
		break;

	case FDLS_STATE_REGISTER_FC4_TYPES:
		FNIC_STD_SET_S_ID(*pfabric_abts, s_id);
		hton24(d_id, FC_FID_DIR_SERV);
		FNIC_STD_SET_D_ID(*pfabric_abts, d_id);
		break;

	case FDLS_STATE_REGISTER_FC4_FEATURES:
		FNIC_STD_SET_S_ID(*pfabric_abts, s_id);
		hton24(d_id, FC_FID_DIR_SERV);
		FNIC_STD_SET_D_ID(*pfabric_abts, d_id);
		break;

	case FDLS_STATE_GPN_FT:
		FNIC_STD_SET_S_ID(*pfabric_abts, s_id);
		hton24(d_id, FC_FID_DIR_SERV);
		FNIC_STD_SET_D_ID(*pfabric_abts, d_id);
		break;
	default:
		return;
	}

	oxid = iport->active_oxid_fabric_req;
	FNIC_STD_SET_OX_ID(*pfabric_abts, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "0x%x: FDLS send fabric abts. iport->fabric.state: %d oxid: 0x%x",
		 iport->fcid, iport->fabric.state, oxid);

	iport->fabric.flags |= FNIC_FDLS_FABRIC_ABORT_ISSUED;

	fnic_send_fcoe_frame(iport, frame, frame_size);

	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
	iport->fabric.timer_pending = 1;
}

static void fdls_send_fabric_flogi(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_flogi *pflogi;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_flogi);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
		     "Failed to allocate frame to send FLOGI");
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	pflogi = (struct fc_std_flogi *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*pflogi = (struct fc_std_flogi) {
		.fchdr = {.fh_r_ctl = FC_RCTL_ELS_REQ, .fh_d_id = {0xFF, 0xFF, 0xFE},
		      .fh_type = FC_TYPE_ELS, .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
		      .fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)},
		.els.fl_cmd = ELS_FLOGI,
		.els.fl_csp = {.sp_hi_ver = FNIC_FC_PH_VER_HI,
			   .sp_lo_ver = FNIC_FC_PH_VER_LO,
			   .sp_bb_cred = cpu_to_be16(FNIC_FC_B2B_CREDIT),
			   .sp_bb_data = cpu_to_be16(FNIC_FC_B2B_RDF_SZ)},
		.els.fl_cssp[2].cp_class = cpu_to_be16(FC_CPC_VALID | FC_CPC_SEQ)
	};

	FNIC_STD_SET_NPORT_NAME(&pflogi->els.fl_wwpn, iport->wwpn);
	FNIC_STD_SET_NODE_NAME(&pflogi->els.fl_wwnn, iport->wwnn);
	FNIC_LOGI_SET_RDF_SIZE(pflogi->els, iport->max_payload_size);
	FNIC_LOGI_SET_R_A_TOV(pflogi->els, iport->r_a_tov);
	FNIC_LOGI_SET_E_D_TOV(pflogi->els, iport->e_d_tov);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_FLOGI,
		&iport->active_oxid_fabric_req);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send FLOGI",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pflogi->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "0x%x: FDLS send fabric FLOGI with oxid: 0x%x", iport->fcid,
		 oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
err_out:
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

static void fdls_send_fabric_plogi(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_flogi *pplogi;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_flogi);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
		     "Failed to allocate frame to send PLOGI");
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	pplogi = (struct fc_std_flogi *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_plogi_frame(frame, iport);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_PLOGI,
		&iport->active_oxid_fabric_req);
	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send fabric PLOGI",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pplogi->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "0x%x: FDLS send fabric PLOGI with oxid: 0x%x", iport->fcid,
		 oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);

err_out:
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

static void fdls_send_rpn_id(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_rpn_id *prpn_id;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_rpn_id);
	uint8_t fcid[3];

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
		     "Failed to allocate frame to send RPN_ID");
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	prpn_id = (struct fc_std_rpn_id *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*prpn_id = (struct fc_std_rpn_id) {
		.fchdr = {.fh_r_ctl = FC_RCTL_DD_UNSOL_CTL,
		      .fh_d_id = {0xFF, 0xFF, 0xFC}, .fh_type = FC_TYPE_CT,
		      .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
		      .fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)},
		.fc_std_ct_hdr = {.ct_rev = FC_CT_REV, .ct_fs_type = FC_FST_DIR,
			      .ct_fs_subtype = FC_NS_SUBTYPE,
			      .ct_cmd = cpu_to_be16(FC_NS_RPN_ID)}
	};

	hton24(fcid, iport->fcid);
	FNIC_STD_SET_S_ID(prpn_id->fchdr, fcid);

	FNIC_STD_SET_PORT_ID(prpn_id->rpn_id, fcid);
	FNIC_STD_SET_PORT_NAME(prpn_id->rpn_id, iport->wwpn);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_RPN,
		&iport->active_oxid_fabric_req);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send RPN_ID",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(prpn_id->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "0x%x: FDLS send RPN ID with oxid: 0x%x", iport->fcid,
		 oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);

err_out:
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

static void fdls_send_scr(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_scr *pscr;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_scr);
	uint8_t fcid[3];

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
		     "Failed to allocate frame to send SCR");
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	pscr = (struct fc_std_scr *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*pscr = (struct fc_std_scr) {
		.fchdr = {.fh_r_ctl = FC_RCTL_ELS_REQ,
		      .fh_d_id = {0xFF, 0xFF, 0xFD}, .fh_type = FC_TYPE_ELS,
		      .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
		      .fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)},
		.scr = {.scr_cmd = ELS_SCR,
		    .scr_reg_func = ELS_SCRF_FULL}
	};

	hton24(fcid, iport->fcid);
	FNIC_STD_SET_S_ID(pscr->fchdr, fcid);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_SCR,
		&iport->active_oxid_fabric_req);
	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send SCR",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pscr->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "0x%x: FDLS send SCR with oxid: 0x%x", iport->fcid,
		 oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);

err_out:
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

static void fdls_send_gpn_ft(struct fnic_iport_s *iport, int fdls_state)
{
	uint8_t *frame;
	struct fc_std_gpn_ft *pgpn_ft;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_gpn_ft);
	uint8_t fcid[3];

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
		     "Failed to allocate frame to send GPN FT");
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	pgpn_ft = (struct fc_std_gpn_ft *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*pgpn_ft = (struct fc_std_gpn_ft) {
		.fchdr = {.fh_r_ctl = FC_RCTL_DD_UNSOL_CTL,
		      .fh_d_id = {0xFF, 0xFF, 0xFC}, .fh_type = FC_TYPE_CT,
		      .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
		      .fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)},
		.fc_std_ct_hdr = {.ct_rev = FC_CT_REV, .ct_fs_type = FC_FST_DIR,
			      .ct_fs_subtype = FC_NS_SUBTYPE,
			      .ct_cmd = cpu_to_be16(FC_NS_GPN_FT)},
		.gpn_ft.fn_fc4_type = 0x08
	};

	hton24(fcid, iport->fcid);
	FNIC_STD_SET_S_ID(pgpn_ft->fchdr, fcid);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_GPN_FT,
		&iport->active_oxid_fabric_req);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send GPN FT",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pgpn_ft->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "0x%x: FDLS send GPN FT with oxid: 0x%x", iport->fcid,
		 oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);

err_out:
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
	fdls_set_state((&iport->fabric), fdls_state);
}

static void fdls_send_register_fc4_types(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_rft_id *prft_id;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_rft_id);
	uint8_t fcid[3];

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
		     "Failed to allocate frame to send RFT");
		return;
	}

	prft_id = (struct fc_std_rft_id *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*prft_id = (struct fc_std_rft_id) {
		.fchdr = {.fh_r_ctl = FC_RCTL_DD_UNSOL_CTL,
		      .fh_d_id = {0xFF, 0xFF, 0xFC}, .fh_type = FC_TYPE_CT,
		      .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
		      .fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)},
		.fc_std_ct_hdr = {.ct_rev = FC_CT_REV, .ct_fs_type = FC_FST_DIR,
			      .ct_fs_subtype = FC_NS_SUBTYPE,
			      .ct_cmd = cpu_to_be16(FC_NS_RFT_ID)}
	};

	hton24(fcid, iport->fcid);
	FNIC_STD_SET_S_ID(prft_id->fchdr, fcid);
	FNIC_STD_SET_PORT_ID(prft_id->rft_id, fcid);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_RFT,
		&iport->active_oxid_fabric_req);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send RFT",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(prft_id->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "0x%x: FDLS send RFT with oxid: 0x%x", iport->fcid,
		 oxid);

	if (IS_FNIC_FCP_INITIATOR(fnic))
		prft_id->rft_id.fr_fts.ff_type_map[0] =
	    cpu_to_be32(1 << FC_TYPE_FCP);

	prft_id->rft_id.fr_fts.ff_type_map[1] =
	cpu_to_be32(1 << (FC_TYPE_CT % FC_NS_BPW));

	fnic_send_fcoe_frame(iport, frame, frame_size);

	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

static void fdls_send_register_fc4_features(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_rff_id *prff_id;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_rff_id);
	uint8_t fcid[3];

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
		     "Failed to allocate frame to send RFF");
		return;
	}

	prff_id = (struct fc_std_rff_id *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*prff_id = (struct fc_std_rff_id) {
		.fchdr = {.fh_r_ctl = FC_RCTL_DD_UNSOL_CTL,
		      .fh_d_id = {0xFF, 0xFF, 0xFC}, .fh_type = FC_TYPE_CT,
		      .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
		      .fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)},
		.fc_std_ct_hdr = {.ct_rev = FC_CT_REV, .ct_fs_type = FC_FST_DIR,
			      .ct_fs_subtype = FC_NS_SUBTYPE,
			      .ct_cmd = cpu_to_be16(FC_NS_RFF_ID)},
		.rff_id.fr_feat = 0x2,
		.rff_id.fr_type = FC_TYPE_FCP
	};

	hton24(fcid, iport->fcid);
	FNIC_STD_SET_S_ID(prff_id->fchdr, fcid);
	FNIC_STD_SET_PORT_ID(prff_id->rff_id, fcid);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_RFF,
		&iport->active_oxid_fabric_req);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			     "0x%x: Failed to allocate OXID to send RFF",
				 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(prff_id->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "0x%x: FDLS send RFF with oxid: 0x%x", iport->fcid,
		 oxid);

	if (IS_FNIC_FCP_INITIATOR(fnic)) {
		prff_id->rff_id.fr_type = FC_TYPE_FCP;
	} else {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "0x%x: Unknown type", iport->fcid);
	}

	fnic_send_fcoe_frame(iport, frame, frame_size);

	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

/**
 * fdls_send_fabric_logo - Send flogo to the fcf
 * @iport: Handle to fnic iport
 *
 * This function does not change or check the fabric state.
 * It the caller's responsibility to set the appropriate iport fabric
 * state when this is called. Normally it is FDLS_STATE_FABRIC_LOGO.
 * Currently this assumes to be called with fnic lock held.
 */
void fdls_send_fabric_logo(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_logo *plogo;
	struct fnic *fnic = iport->fnic;
	uint8_t d_id[3];
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_logo);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->lport->host, fnic->fnic_num,
		     "Failed to allocate frame to send fabric LOGO");
		return;
	}

	plogo = (struct fc_std_logo *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_logo_frame(frame, iport);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_LOGO,
		&iport->active_oxid_fabric_req);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send fabric LOGO",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(plogo->fchdr, oxid);

	hton24(d_id, FC_FID_FLOGI);
	FNIC_STD_SET_D_ID(plogo->fchdr, d_id);

	iport->fabric.flags &= ~FNIC_FDLS_FABRIC_ABORT_ISSUED;

		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "0x%x: FDLS send fabric LOGO with oxid: 0x%x",
		 iport->fcid, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);

	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

void fdls_fabric_timer_callback(struct timer_list *t)
{
	struct fnic_fdls_fabric_s *fabric = from_timer(fabric, t, retry_timer);
	struct fnic_iport_s *iport =
		container_of(fabric, struct fnic_iport_s, fabric);
	struct fnic *fnic = iport->fnic;
	unsigned long flags;

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
		 "tp: %d fab state: %d fab retry counter: %d max_flogi_retries: %d",
		 iport->fabric.timer_pending, iport->fabric.state,
		 iport->fabric.retry_counter, iport->max_flogi_retries);

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (!iport->fabric.timer_pending) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	if (iport->fabric.del_timer_inprogress) {
		iport->fabric.del_timer_inprogress = 0;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "fabric_del_timer inprogress(%d). Skip timer cb",
					 iport->fabric.del_timer_inprogress);
		return;
	}

	iport->fabric.timer_pending = 0;

	/* The fabric state indicates which frames have time out, and we retry */
	switch (iport->fabric.state) {
	case FDLS_STATE_FABRIC_FLOGI:
		/* Flogi received a LS_RJT with busy we retry from here */
		if ((iport->fabric.flags & FNIC_FDLS_RETRY_FRAME)
			&& (iport->fabric.retry_counter < iport->max_flogi_retries)) {
			iport->fabric.flags &= ~FNIC_FDLS_RETRY_FRAME;
			fdls_send_fabric_flogi(iport);
		} else if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED)) {
			/* Flogi has time out 2*ed_tov send abts */
			fdls_send_fabric_abts(iport);
		} else {
			/* ABTS has timed out
			 * Mark the OXID to be freed after 2 * r_a_tov and retry the req
			 */
			fdls_schedule_oxid_free(iport, &iport->active_oxid_fabric_req);
			if (iport->fabric.retry_counter < iport->max_flogi_retries) {
				iport->fabric.flags &= ~FNIC_FDLS_FABRIC_ABORT_ISSUED;
				fdls_send_fabric_flogi(iport);
			} else
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "Exceeded max FLOGI retries");
		}
		break;
	case FDLS_STATE_FABRIC_PLOGI:
		/* Plogi received a LS_RJT with busy we retry from here */
		if ((iport->fabric.flags & FNIC_FDLS_RETRY_FRAME)
			&& (iport->fabric.retry_counter < iport->max_plogi_retries)) {
			iport->fabric.flags &= ~FNIC_FDLS_RETRY_FRAME;
			fdls_send_fabric_plogi(iport);
		} else if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED)) {
		/* Plogi has timed out 2*ed_tov send abts */
			fdls_send_fabric_abts(iport);
		} else {
			/* ABTS has timed out
			 * Mark the OXID to be freed after 2 * r_a_tov and retry the req
			 */
			fdls_schedule_oxid_free(iport, &iport->active_oxid_fabric_req);
			if (iport->fabric.retry_counter < iport->max_plogi_retries) {
				iport->fabric.flags &= ~FNIC_FDLS_FABRIC_ABORT_ISSUED;
				fdls_send_fabric_plogi(iport);
			} else
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "Exceeded max PLOGI retries");
		}
		break;
	case FDLS_STATE_RPN_ID:
		/* Rpn_id received a LS_RJT with busy we retry from here */
		if ((iport->fabric.flags & FNIC_FDLS_RETRY_FRAME)
			&& (iport->fabric.retry_counter < FDLS_RETRY_COUNT)) {
			iport->fabric.flags &= ~FNIC_FDLS_RETRY_FRAME;
			fdls_send_rpn_id(iport);
		} else if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED))
			/* RPN has timed out. Send abts */
			fdls_send_fabric_abts(iport);
		else {
			/* ABTS has timed out */
			fdls_schedule_oxid_free(iport, &iport->active_oxid_fabric_req);
			fnic_fdls_start_plogi(iport);	/* go back to fabric Plogi */
		}
		break;
	case FDLS_STATE_SCR:
		/* scr received a LS_RJT with busy we retry from here */
		if ((iport->fabric.flags & FNIC_FDLS_RETRY_FRAME)
			&& (iport->fabric.retry_counter < FDLS_RETRY_COUNT)) {
			iport->fabric.flags &= ~FNIC_FDLS_RETRY_FRAME;
			fdls_send_scr(iport);
		} else if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED))
			/* scr has timed out. Send abts */
			fdls_send_fabric_abts(iport);
		else {
			/* ABTS has timed out */
			fdls_schedule_oxid_free(iport, &iport->active_oxid_fabric_req);
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "ABTS timed out. Starting PLOGI: %p", iport);
			fnic_fdls_start_plogi(iport);
		}
		break;
	case FDLS_STATE_REGISTER_FC4_TYPES:
		/* scr received a LS_RJT with busy we retry from here */
		if ((iport->fabric.flags & FNIC_FDLS_RETRY_FRAME)
			&& (iport->fabric.retry_counter < FDLS_RETRY_COUNT)) {
			iport->fabric.flags &= ~FNIC_FDLS_RETRY_FRAME;
			fdls_send_register_fc4_types(iport);
		} else if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED)) {
			/* RFT_ID timed out send abts */
			fdls_send_fabric_abts(iport);
		} else {
			/* ABTS has timed out */
			fdls_schedule_oxid_free(iport, &iport->active_oxid_fabric_req);
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "ABTS timed out. Starting PLOGI: %p", iport);
			fnic_fdls_start_plogi(iport);	/* go back to fabric Plogi */
		}
		break;
	case FDLS_STATE_REGISTER_FC4_FEATURES:
		/* scr received a LS_RJT with busy we retry from here */
		if ((iport->fabric.flags & FNIC_FDLS_RETRY_FRAME)
			&& (iport->fabric.retry_counter < FDLS_RETRY_COUNT)) {
			iport->fabric.flags &= ~FNIC_FDLS_RETRY_FRAME;
			fdls_send_register_fc4_features(iport);
		} else if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED))
			/* SCR has timed out. Send abts */
			fdls_send_fabric_abts(iport);
		else {
			/* ABTS has timed out */
			fdls_schedule_oxid_free(iport, &iport->active_oxid_fabric_req);
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "ABTS timed out. Starting PLOGI %p", iport);
			fnic_fdls_start_plogi(iport);	/* go back to fabric Plogi */
		}
		break;
	case FDLS_STATE_RSCN_GPN_FT:
	case FDLS_STATE_SEND_GPNFT:
	case FDLS_STATE_GPN_FT:
		/* GPN_FT received a LS_RJT with busy we retry from here */
		if ((iport->fabric.flags & FNIC_FDLS_RETRY_FRAME)
			&& (iport->fabric.retry_counter < FDLS_RETRY_COUNT)) {
			iport->fabric.flags &= ~FNIC_FDLS_RETRY_FRAME;
			fdls_send_gpn_ft(iport, iport->fabric.state);
		} else if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED)) {
			/* gpn_ft has timed out. Send abts */
			fdls_send_fabric_abts(iport);
		} else {
			/* ABTS has timed out */
			fdls_schedule_oxid_free(iport, &iport->active_oxid_fabric_req);
			if (iport->fabric.retry_counter < FDLS_RETRY_COUNT) {
				fdls_send_gpn_ft(iport, iport->fabric.state);
			} else {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "ABTS timeout for fabric GPN_FT. Check name server: %p",
					 iport);
			}
		}
		break;
	default:
		fnic_fdls_start_flogi(iport);	/* Placeholder call */
		break;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}


static void fnic_fdls_start_flogi(struct fnic_iport_s *iport)
{
	iport->fabric.retry_counter = 0;
	fdls_send_fabric_flogi(iport);
	fdls_set_state((&iport->fabric), FDLS_STATE_FABRIC_FLOGI);
	iport->fabric.flags = 0;
}

static void fnic_fdls_start_plogi(struct fnic_iport_s *iport)
{
	iport->fabric.retry_counter = 0;
	fdls_send_fabric_plogi(iport);
	fdls_set_state((&iport->fabric), FDLS_STATE_FABRIC_PLOGI);
	iport->fabric.flags &= ~FNIC_FDLS_FABRIC_ABORT_ISSUED;
}


static void
fdls_process_rff_id_rsp(struct fnic_iport_s *iport,
			struct fc_frame_header *fchdr)
{
	struct fnic *fnic = iport->fnic;
	struct fnic_fdls_fabric_s *fdls = &iport->fabric;
	struct fc_std_rff_id *rff_rsp = (struct fc_std_rff_id *) fchdr;
	uint16_t rsp;
	uint8_t reason_code;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	if (fdls_get_state(fdls) != FDLS_STATE_REGISTER_FC4_FEATURES) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "RFF_ID resp recvd in state(%d). Dropping.",
					 fdls_get_state(fdls));
		return;
	}

	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
		return;
	}

	rsp = FNIC_STD_GET_FC_CT_CMD((&rff_rsp->fc_std_ct_hdr));
	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "0x%x: FDLS process RFF ID response: 0x%04x", iport->fcid,
				 (uint32_t) rsp);

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (rsp) {
	case FC_FS_ACC:
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "Canceling fabric disc timer %p\n", iport);
			fnic_del_fabric_timer_sync(fnic);
		}
		iport->fabric.timer_pending = 0;
		fdls->retry_counter = 0;
		fdls_set_state((&iport->fabric), FDLS_STATE_SCR);
		fdls_send_scr(iport);
		break;
	case FC_FS_RJT:
		reason_code = rff_rsp->fc_std_ct_hdr.ct_reason;
		if (((reason_code == FC_FS_RJT_BSY)
			|| (reason_code == FC_FS_RJT_UNABL))
			&& (fdls->retry_counter < FDLS_RETRY_COUNT)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "RFF_ID ret ELS_LS_RJT BUSY. Retry from timer routine %p",
					 iport);

			/* Retry again from the timer routine */
			fdls->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "RFF_ID returned ELS_LS_RJT. Halting discovery %p",
			 iport);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "Canceling fabric disc timer %p\n", iport);
				fnic_del_fabric_timer_sync(fnic);
			}
			fdls->timer_pending = 0;
			fdls->retry_counter = 0;
		}
		break;
	default:
		break;
	}
}

static void
fdls_process_rft_id_rsp(struct fnic_iport_s *iport,
			struct fc_frame_header *fchdr)
{
	struct fnic_fdls_fabric_s *fdls = &iport->fabric;
	struct fc_std_rft_id *rft_rsp = (struct fc_std_rft_id *) fchdr;
	uint16_t rsp;
	uint8_t reason_code;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	if (fdls_get_state(fdls) != FDLS_STATE_REGISTER_FC4_TYPES) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "RFT_ID resp recvd in state(%d). Dropping.",
					 fdls_get_state(fdls));
		return;
	}

	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
		return;
	}


	rsp = FNIC_STD_GET_FC_CT_CMD((&rft_rsp->fc_std_ct_hdr));
	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "0x%x: FDLS process RFT ID response: 0x%04x", iport->fcid,
				 (uint32_t) rsp);

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (rsp) {
	case FC_FS_ACC:
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "Canceling fabric disc timer %p\n", iport);
			fnic_del_fabric_timer_sync(fnic);
		}
		iport->fabric.timer_pending = 0;
		fdls->retry_counter = 0;
		fdls_send_register_fc4_features(iport);
		fdls_set_state((&iport->fabric), FDLS_STATE_REGISTER_FC4_FEATURES);
		break;
	case FC_FS_RJT:
		reason_code = rft_rsp->fc_std_ct_hdr.ct_reason;
		if (((reason_code == FC_FS_RJT_BSY)
			|| (reason_code == FC_FS_RJT_UNABL))
			&& (fdls->retry_counter < FDLS_RETRY_COUNT)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "0x%x: RFT_ID ret ELS_LS_RJT BUSY. Retry from timer routine",
				 iport->fcid);

			/* Retry again from the timer routine */
			fdls->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "0x%x: RFT_ID REJ. Halting discovery reason %d expl %d",
				 iport->fcid, reason_code,
			 rft_rsp->fc_std_ct_hdr.ct_explan);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "Canceling fabric disc timer %p\n", iport);
				fnic_del_fabric_timer_sync(fnic);
			}
			fdls->timer_pending = 0;
			fdls->retry_counter = 0;
		}
		break;
	default:
		break;
	}
}

static void
fdls_process_rpn_id_rsp(struct fnic_iport_s *iport,
			struct fc_frame_header *fchdr)
{
	struct fnic_fdls_fabric_s *fdls = &iport->fabric;
	struct fc_std_rpn_id *rpn_rsp = (struct fc_std_rpn_id *) fchdr;
	uint16_t rsp;
	uint8_t reason_code;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	if (fdls_get_state(fdls) != FDLS_STATE_RPN_ID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "RPN_ID resp recvd in state(%d). Dropping.",
					 fdls_get_state(fdls));
		return;
	}
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
		return;
	}

	rsp = FNIC_STD_GET_FC_CT_CMD((&rpn_rsp->fc_std_ct_hdr));
	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "0x%x: FDLS process RPN ID response: 0x%04x", iport->fcid,
				 (uint32_t) rsp);
	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (rsp) {
	case FC_FS_ACC:
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "Canceling fabric disc timer %p\n", iport);
			fnic_del_fabric_timer_sync(fnic);
		}
		iport->fabric.timer_pending = 0;
		fdls->retry_counter = 0;
		fdls_send_register_fc4_types(iport);
		fdls_set_state((&iport->fabric), FDLS_STATE_REGISTER_FC4_TYPES);
		break;
	case FC_FS_RJT:
		reason_code = rpn_rsp->fc_std_ct_hdr.ct_reason;
		if (((reason_code == FC_FS_RJT_BSY)
			|| (reason_code == FC_FS_RJT_UNABL))
			&& (fdls->retry_counter < FDLS_RETRY_COUNT)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "RPN_ID returned REJ BUSY. Retry from timer routine %p",
					 iport);

			/* Retry again from the timer routine */
			fdls->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "RPN_ID ELS_LS_RJT. Halting discovery %p", iport);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "Canceling fabric disc timer %p\n", iport);
				fnic_del_fabric_timer_sync(fnic);
			}
			fdls->timer_pending = 0;
			fdls->retry_counter = 0;
		}
		break;
	default:
		break;
	}
}

static void
fdls_process_scr_rsp(struct fnic_iport_s *iport,
		     struct fc_frame_header *fchdr)
{
	struct fnic_fdls_fabric_s *fdls = &iport->fabric;
	struct fc_std_scr *scr_rsp = (struct fc_std_scr *) fchdr;
	struct fc_std_els_rjt_rsp *els_rjt = (struct fc_std_els_rjt_rsp *) fchdr;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "FDLS process SCR response: 0x%04x",
		 (uint32_t) scr_rsp->scr.scr_cmd);

	if (fdls_get_state(fdls) != FDLS_STATE_SCR) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "SCR resp recvd in state(%d). Dropping.",
					 fdls_get_state(fdls));
		return;
	}
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
	}

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (scr_rsp->scr.scr_cmd) {
	case ELS_LS_ACC:
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "Canceling fabric disc timer %p\n", iport);
			fnic_del_fabric_timer_sync(fnic);
		}
		iport->fabric.timer_pending = 0;
		iport->fabric.retry_counter = 0;
		fdls_send_gpn_ft(iport, FDLS_STATE_GPN_FT);
		break;

	case ELS_LS_RJT:

		if (((els_rjt->rej.er_reason == ELS_RJT_BUSY)
	     || (els_rjt->rej.er_reason == ELS_RJT_UNAB))
			&& (fdls->retry_counter < FDLS_RETRY_COUNT)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "SCR ELS_LS_RJT BUSY. Retry from timer routine %p",
						 iport);
			/* Retry again from the timer routine */
			fdls->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "SCR returned ELS_LS_RJT. Halting discovery %p",
						 iport);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "Canceling fabric disc timer %p\n", iport);
				fnic_del_fabric_timer_sync(fnic);
			}
			fdls->timer_pending = 0;
			fdls->retry_counter = 0;
		}
		break;

	default:
		break;
	}
}



static void
fdls_process_gpn_ft_rsp(struct fnic_iport_s *iport,
			struct fc_frame_header *fchdr, int len)
{
	struct fnic_fdls_fabric_s *fdls = &iport->fabric;
	struct fc_std_gpn_ft *gpn_ft_rsp = (struct fc_std_gpn_ft *) fchdr;
	uint16_t rsp;
	uint8_t reason_code;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "FDLS process GPN_FT response: iport state: %d len: %d",
				 iport->state, len);

	/*
	 * GPNFT response :-
	 *  FDLS_STATE_GPN_FT      : GPNFT send after SCR state
	 *  during fabric discovery(FNIC_IPORT_STATE_FABRIC_DISC)
	 *  FDLS_STATE_RSCN_GPN_FT : GPNFT send in response to RSCN
	 *  FDLS_STATE_SEND_GPNFT  : GPNFT send after deleting a Target,
	 *  e.g. after receiving Target LOGO
	 *  FDLS_STATE_TGT_DISCOVERY :Target discovery is currently in progress
	 *  from previous GPNFT response,a new GPNFT response has come.
	 */
	if (!(((iport->state == FNIC_IPORT_STATE_FABRIC_DISC)
		   && (fdls_get_state(fdls) == FDLS_STATE_GPN_FT))
		  || ((iport->state == FNIC_IPORT_STATE_READY)
			  && ((fdls_get_state(fdls) == FDLS_STATE_RSCN_GPN_FT)
				  || (fdls_get_state(fdls) == FDLS_STATE_SEND_GPNFT)
				  || (fdls_get_state(fdls) == FDLS_STATE_TGT_DISCOVERY))))) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "GPNFT resp recvd in fab state(%d) iport_state(%d). Dropping.",
			 fdls_get_state(fdls), iport->state);
		return;
	}

	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
	}

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	iport->state = FNIC_IPORT_STATE_READY;
	rsp = FNIC_STD_GET_FC_CT_CMD((&gpn_ft_rsp->fc_std_ct_hdr));

	switch (rsp) {

	case FC_FS_ACC:
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "0x%x: GPNFT_RSP accept", iport->fcid);
		break;

	case FC_FS_RJT:
		reason_code = gpn_ft_rsp->fc_std_ct_hdr.ct_reason;
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "0x%x: GPNFT_RSP Reject reason: %d", iport->fcid, reason_code);
		break;

	default:
		break;
	}
}

/**
 * fdls_process_fabric_logo_rsp - Handle an flogo response from the fcf
 * @iport: Handle to fnic iport
 * @fchdr: Incoming frame
 */
static void
fdls_process_fabric_logo_rsp(struct fnic_iport_s *iport,
			     struct fc_frame_header *fchdr)
{
	struct fc_std_flogi *flogo_rsp = (struct fc_std_flogi *) fchdr;
	struct fnic_fdls_fabric_s *fdls = &iport->fabric;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
	}
	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (flogo_rsp->els.fl_cmd) {
	case ELS_LS_ACC:
		if (iport->fabric.state != FDLS_STATE_FABRIC_LOGO) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "Flogo response. Fabric not in LOGO state. Dropping! %p",
				 iport);
			return;
		}

		iport->fabric.state = FDLS_STATE_FLOGO_DONE;
		iport->state = FNIC_IPORT_STATE_LINK_WAIT;

		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "iport 0x%p Canceling fabric disc timer\n",
						 iport);
			fnic_del_fabric_timer_sync(fnic);
		}
		iport->fabric.timer_pending = 0;
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Flogo response from Fabric for did: 0x%x",
		     ntoh24(fchdr->fh_d_id));
		return;

	case ELS_LS_RJT:
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "Flogo response from Fabric for did: 0x%x returned ELS_LS_RJT",
		     ntoh24(fchdr->fh_d_id));
		return;

	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "FLOGO response not accepted or rejected: 0x%x",
		     flogo_rsp->els.fl_cmd);
	}
}

static void
fdls_process_flogi_rsp(struct fnic_iport_s *iport,
		       struct fc_frame_header *fchdr, void *rx_frame)
{
	struct fnic_fdls_fabric_s *fabric = &iport->fabric;
	struct fc_std_flogi *flogi_rsp = (struct fc_std_flogi *) fchdr;
	uint8_t *fcid;
	uint16_t rdf_size;
	uint8_t fcmac[6] = { 0x0E, 0XFC, 0x00, 0x00, 0x00, 0x00 };
	struct fnic *fnic = iport->fnic;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "0x%x: FDLS processing FLOGI response", iport->fcid);

	if (fdls_get_state(fabric) != FDLS_STATE_FABRIC_FLOGI) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "FLOGI response received in state (%d). Dropping frame",
					 fdls_get_state(fabric));
		return;
	}
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fabric), oxid, iport->active_oxid_fabric_req);
		return;
	}

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (flogi_rsp->els.fl_cmd) {
	case ELS_LS_ACC:
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "iport fcid: 0x%x Canceling fabric disc timer\n",
						 iport->fcid);
			fnic_del_fabric_timer_sync(fnic);
		}

		iport->fabric.timer_pending = 0;
		iport->fabric.retry_counter = 0;
		fcid = FNIC_STD_GET_D_ID(fchdr);
		iport->fcid = ntoh24(fcid);
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "0x%x: FLOGI response accepted", iport->fcid);

		/* Learn the Service Params */
		rdf_size = be16_to_cpu(FNIC_LOGI_RDF_SIZE(flogi_rsp->els));
		if ((rdf_size >= FNIC_MIN_DATA_FIELD_SIZE)
			&& (rdf_size < FNIC_FC_MAX_PAYLOAD_LEN))
			iport->max_payload_size = min(rdf_size,
								  iport->max_payload_size);

		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "max_payload_size from fabric: %u set: %d", rdf_size,
					 iport->max_payload_size);

		iport->r_a_tov = be32_to_cpu(FNIC_LOGI_R_A_TOV(flogi_rsp->els));
		iport->e_d_tov = be32_to_cpu(FNIC_LOGI_E_D_TOV(flogi_rsp->els));

		if (FNIC_LOGI_FEATURES(flogi_rsp->els) & FNIC_FC_EDTOV_NSEC)
			iport->e_d_tov = iport->e_d_tov / FNIC_NSEC_TO_MSEC;

		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "From fabric: R_A_TOV: %d E_D_TOV: %d",
					 iport->r_a_tov, iport->e_d_tov);

		if (IS_FNIC_FCP_INITIATOR(fnic)) {
			fc_host_fabric_name(iport->fnic->lport->host) =
			get_unaligned_be64(&FNIC_LOGI_NODE_NAME(flogi_rsp->els));
			fc_host_port_id(iport->fnic->lport->host) = iport->fcid;
		}

		fnic_fdls_learn_fcoe_macs(iport, rx_frame, fcid);

		memcpy(&fcmac[3], fcid, 3);
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "Adding vNIC device MAC addr: %02x:%02x:%02x:%02x:%02x:%02x",
			 fcmac[0], fcmac[1], fcmac[2], fcmac[3], fcmac[4],
			 fcmac[5]);
		vnic_dev_add_addr(iport->fnic->vdev, fcmac);

		if (fdls_get_state(fabric) == FDLS_STATE_FABRIC_FLOGI) {
			fnic_fdls_start_plogi(iport);
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "FLOGI response received. Starting PLOGI");
		} else {
			/* From FDLS_STATE_FABRIC_FLOGI state fabric can only go to
			 * FDLS_STATE_LINKDOWN
			 * state, hence we don't have to worry about undoing:
			 * the fnic_fdls_register_portid and vnic_dev_add_addr
			 */
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "FLOGI response received in state (%d). Dropping frame",
				 fdls_get_state(fabric));
		}
		break;

	case ELS_LS_RJT:
		if (fabric->retry_counter < iport->max_flogi_retries) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "FLOGI returned ELS_LS_RJT BUSY. Retry from timer routine %p",
				 iport);

			/* Retry Flogi again from the timer routine. */
			fabric->flags |= FNIC_FDLS_RETRY_FRAME;

		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "FLOGI returned ELS_LS_RJT. Halting discovery %p",
			 iport);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "iport 0x%p Canceling fabric disc timer\n",
							 iport);
				fnic_del_fabric_timer_sync(fnic);
			}
			fabric->timer_pending = 0;
			fabric->retry_counter = 0;
		}
		break;

	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "FLOGI response not accepted: 0x%x",
		     flogi_rsp->els.fl_cmd);
		break;
	}
}

static void
fdls_process_fabric_plogi_rsp(struct fnic_iport_s *iport,
			      struct fc_frame_header *fchdr)
{
	struct fc_std_flogi *plogi_rsp = (struct fc_std_flogi *) fchdr;
	struct fc_std_els_rjt_rsp *els_rjt = (struct fc_std_els_rjt_rsp *) fchdr;
	struct fnic_fdls_fabric_s *fdls = &iport->fabric;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	if (fdls_get_state((&iport->fabric)) != FDLS_STATE_FABRIC_PLOGI) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "Fabric PLOGI response received in state (%d). Dropping frame",
			 fdls_get_state(&iport->fabric));
		return;
	}
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
		return;
	}
	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (plogi_rsp->els.fl_cmd) {
	case ELS_LS_ACC:
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "iport fcid: 0x%x fabric PLOGI response: Accepted\n",
				 iport->fcid);
			fnic_del_fabric_timer_sync(fnic);
		}
		iport->fabric.timer_pending = 0;
		iport->fabric.retry_counter = 0;
		fdls_set_state(&iport->fabric, FDLS_STATE_RPN_ID);
		fdls_send_rpn_id(iport);
		break;
	case ELS_LS_RJT:

		if (((els_rjt->rej.er_reason == ELS_RJT_BUSY)
	     || (els_rjt->rej.er_reason == ELS_RJT_UNAB))
			&& (iport->fabric.retry_counter < iport->max_plogi_retries)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "0x%x: Fabric PLOGI ELS_LS_RJT BUSY. Retry from timer routine",
				 iport->fcid);
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "0x%x: Fabric PLOGI ELS_LS_RJT. Halting discovery",
				 iport->fcid);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "iport fcid: 0x%x Canceling fabric disc timer\n",
							 iport->fcid);
				fnic_del_fabric_timer_sync(fnic);
			}
			iport->fabric.timer_pending = 0;
			iport->fabric.retry_counter = 0;
			return;
		}
		break;
	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "PLOGI response not accepted: 0x%x",
		     plogi_rsp->els.fl_cmd);
		break;
	}
}

static void
fdls_process_fabric_abts_rsp(struct fnic_iport_s *iport,
			     struct fc_frame_header *fchdr)
{
	uint32_t s_id;
	struct fc_std_abts_ba_acc *ba_acc = (struct fc_std_abts_ba_acc *)fchdr;
	struct fc_std_abts_ba_rjt *ba_rjt;
	uint32_t fabric_state = iport->fabric.state;
	struct fnic *fnic = iport->fnic;
	int frame_type;
	uint16_t oxid;

	s_id = ntoh24(fchdr->fh_s_id);
	ba_rjt = (struct fc_std_abts_ba_rjt *) fchdr;

	if (!((s_id == FC_FID_DIR_SERV) || (s_id == FC_FID_FLOGI)
		  || (s_id == FC_FID_FCTRL))) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "Received abts rsp with invalid SID: 0x%x. Dropping frame",
			 s_id);
		return;
	}

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Received abts rsp with invalid oxid: 0x%x. Dropping frame",
			oxid);
		return;
	}

	if (iport->fabric.timer_pending) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Canceling fabric disc timer %p\n", iport);
		fnic_del_fabric_timer_sync(fnic);
	}
	iport->fabric.timer_pending = 0;
	iport->fabric.flags &= ~FNIC_FDLS_FABRIC_ABORT_ISSUED;

	if (fchdr->fh_r_ctl == FC_RCTL_BA_ACC) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "Received abts rsp BA_ACC for fabric_state: %d OX_ID: 0x%x",
		     fabric_state, be16_to_cpu(ba_acc->acc.ba_ox_id));
	} else if (fchdr->fh_r_ctl == FC_RCTL_BA_RJT) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "BA_RJT fs: %d OX_ID: 0x%x rc: 0x%x rce: 0x%x",
		     fabric_state, FNIC_STD_GET_OX_ID(&ba_rjt->fchdr),
		     ba_rjt->rjt.br_reason, ba_rjt->rjt.br_explan);
	}

	frame_type = FNIC_FRAME_TYPE(oxid);
	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	/* currently error handling/retry logic is same for ABTS BA_ACC & BA_RJT */
	switch (frame_type) {
	case FNIC_FRAME_TYPE_FABRIC_FLOGI:
		if (iport->fabric.retry_counter < iport->max_flogi_retries)
			fdls_send_fabric_flogi(iport);
		else
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "Exceeded max FLOGI retries");
		break;
	case FNIC_FRAME_TYPE_FABRIC_LOGO:
		if (iport->fabric.retry_counter < FABRIC_LOGO_MAX_RETRY)
			fdls_send_fabric_logo(iport);
		break;
	case FNIC_FRAME_TYPE_FABRIC_PLOGI:
		if (iport->fabric.retry_counter < iport->max_plogi_retries)
			fdls_send_fabric_plogi(iport);
		else
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "Exceeded max PLOGI retries");
		break;
	case FNIC_FRAME_TYPE_FABRIC_RPN:
		if (iport->fabric.retry_counter < FDLS_RETRY_COUNT)
			fdls_send_rpn_id(iport);
		else
			/* go back to fabric Plogi */
			fnic_fdls_start_plogi(iport);
		break;
	case FNIC_FRAME_TYPE_FABRIC_SCR:
		if (iport->fabric.retry_counter < FDLS_RETRY_COUNT)
			fdls_send_scr(iport);
		else {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				"SCR exhausted retries. Start fabric PLOGI %p",
				 iport);
			fnic_fdls_start_plogi(iport);	/* go back to fabric Plogi */
		}
		break;
	case FNIC_FRAME_TYPE_FABRIC_RFT:
		if (iport->fabric.retry_counter < FDLS_RETRY_COUNT)
			fdls_send_register_fc4_types(iport);
		else {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				"RFT exhausted retries. Start fabric PLOGI %p",
				 iport);
			fnic_fdls_start_plogi(iport);	/* go back to fabric Plogi */
		}
		break;
	case FNIC_FRAME_TYPE_FABRIC_RFF:
		if (iport->fabric.retry_counter < FDLS_RETRY_COUNT)
			fdls_send_register_fc4_features(iport);
		else {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				"RFF exhausted retries. Start fabric PLOGI %p",
				 iport);
			fnic_fdls_start_plogi(iport);	/* go back to fabric Plogi */
		}
		break;
	case FNIC_FRAME_TYPE_FABRIC_GPN_FT:
		if (iport->fabric.retry_counter <= FDLS_RETRY_COUNT)
			fdls_send_gpn_ft(iport, fabric_state);
		else
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				"GPN FT exhausted retries. Start fabric PLOGI %p",
				iport);
		break;
	default:
		/*
		 * We should not be here since we already validated rx oxid with
		 * our active_oxid_fabric_req
		 */
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Invalid OXID/active oxid 0x%x\n", oxid);
		WARN_ON(true);
		return;
	}
}

/*
 * Performs a validation for all FCOE frames and return the frame type
 */
int
fnic_fdls_validate_and_get_frame_type(struct fnic_iport_s *iport,
	struct fc_frame_header *fchdr)
{
	uint8_t type;
	uint8_t *fc_payload;
	uint16_t oxid;
	uint32_t s_id;
	uint32_t d_id;
	struct fnic *fnic = iport->fnic;
	struct fnic_fdls_fabric_s *fabric = &iport->fabric;
	int oxid_frame_type;

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	fc_payload = (uint8_t *) fchdr + sizeof(struct fc_frame_header);
	type = *fc_payload;
	s_id = ntoh24(fchdr->fh_s_id);
	d_id = ntoh24(fchdr->fh_d_id);

	/* some common validation */
		if (fdls_get_state(fabric) > FDLS_STATE_FABRIC_FLOGI) {
			if ((iport->fcid != d_id) || (!FNIC_FC_FRAME_CS_CTL(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "invalid frame received. Dropping frame");
				return -1;
			}
		}

	/*  BLS ABTS response */
	if ((fchdr->fh_r_ctl == FC_RCTL_BA_ACC)
	|| (fchdr->fh_r_ctl == FC_RCTL_BA_RJT)) {
		if (!(FNIC_FC_FRAME_TYPE_BLS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "Received ABTS invalid frame. Dropping frame");
			return -1;

		}
		if (fdls_is_oxid_fabric_req(oxid)) {
			if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED)) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					"Received unexpected ABTS RSP(oxid:0x%x) from 0x%x. Dropping frame",
					oxid, s_id);
				return -1;
	}
			return FNIC_FABRIC_BLS_ABTS_RSP;
		} else if (fdls_is_oxid_fdmi_req(oxid)) {
			return FNIC_FDMI_BLS_ABTS_RSP;
		} else if (fdls_is_oxid_tgt_req(oxid)) {
			return FNIC_TPORT_BLS_ABTS_RSP;
		}
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"Received ABTS rsp with unknown oxid(0x%x) from 0x%x. Dropping frame",
			oxid, s_id);
		return -1;
	}

	/* BLS ABTS Req */
	if ((fchdr->fh_r_ctl == FC_RCTL_BA_ABTS)
	&& (FNIC_FC_FRAME_TYPE_BLS(fchdr))) {
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Receiving Abort Request from s_id: 0x%x", s_id);
		return FNIC_BLS_ABTS_REQ;
	}

	/* unsolicited requests frames */
	if (FNIC_FC_FRAME_UNSOLICITED(fchdr)) {
		switch (type) {
		case ELS_LOGO:
			if ((!FNIC_FC_FRAME_FCTL_FIRST_LAST_SEQINIT(fchdr))
				|| (!FNIC_FC_FRAME_UNSOLICITED(fchdr))
				|| (!FNIC_FC_FRAME_TYPE_ELS(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
							 "Received LOGO invalid frame. Dropping frame");
				return -1;
			}
			return FNIC_ELS_LOGO_REQ;
		case ELS_RSCN:
			if ((!FNIC_FC_FRAME_FCTL_FIRST_LAST_SEQINIT(fchdr))
				|| (!FNIC_FC_FRAME_TYPE_ELS(fchdr))
				|| (!FNIC_FC_FRAME_UNSOLICITED(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
						 "Received RSCN invalid FCTL. Dropping frame");
				return -1;
			}
			if (s_id != FC_FID_FCTRL)
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				     "Received RSCN from target FCTL: 0x%x type: 0x%x s_id: 0x%x.",
				     fchdr->fh_f_ctl[0], fchdr->fh_type, s_id);
			return FNIC_ELS_RSCN_REQ;
		case ELS_PLOGI:
			return FNIC_ELS_PLOGI_REQ;
		case ELS_ECHO:
			return FNIC_ELS_ECHO_REQ;
		case ELS_ADISC:
			return FNIC_ELS_ADISC;
		case ELS_RLS:
			return FNIC_ELS_RLS;
		case ELS_RRQ:
			return FNIC_ELS_RRQ;
		default:
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
				 "Unsupported frame (type:0x%02x) from fcid: 0x%x",
				 type, s_id);
			return FNIC_ELS_UNSUPPORTED_REQ;
		}
	}

	/* solicited response from fabric or target */
	oxid_frame_type = FNIC_FRAME_TYPE(oxid);
	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			"oxid frame code: 0x%x, oxid: 0x%x\n", oxid_frame_type, oxid);
	switch (oxid_frame_type) {
	case FNIC_FRAME_TYPE_FABRIC_FLOGI:
		if (type == ELS_LS_ACC) {
			if ((s_id != FC_FID_FLOGI)
				|| (!FNIC_FC_FRAME_TYPE_ELS(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
				return -1;
			}
		}
		return FNIC_FABRIC_FLOGI_RSP;

	case FNIC_FRAME_TYPE_FABRIC_PLOGI:
		if (type == ELS_LS_ACC) {
			if ((s_id != FC_FID_DIR_SERV)
				|| (!FNIC_FC_FRAME_TYPE_ELS(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
				return -1;
			}
		}
		return FNIC_FABRIC_PLOGI_RSP;

	case FNIC_FRAME_TYPE_FABRIC_SCR:
		if (type == ELS_LS_ACC) {
			if ((s_id != FC_FID_FCTRL)
				|| (!FNIC_FC_FRAME_TYPE_ELS(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
				return -1;
			}
		}
		return FNIC_FABRIC_SCR_RSP;

	case FNIC_FRAME_TYPE_FABRIC_RPN:
		if ((s_id != FC_FID_DIR_SERV) || (!FNIC_FC_FRAME_TYPE_FC_GS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
			return -1;
		}
		return FNIC_FABRIC_RPN_RSP;

	case FNIC_FRAME_TYPE_FABRIC_RFT:
		if ((s_id != FC_FID_DIR_SERV) || (!FNIC_FC_FRAME_TYPE_FC_GS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
			return -1;
		}
		return FNIC_FABRIC_RFT_RSP;

	case FNIC_FRAME_TYPE_FABRIC_RFF:
		if ((s_id != FC_FID_DIR_SERV) || (!FNIC_FC_FRAME_TYPE_FC_GS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
			return -1;
		}
		return FNIC_FABRIC_RFF_RSP;

	case FNIC_FRAME_TYPE_FABRIC_GPN_FT:
		if ((s_id != FC_FID_DIR_SERV) || (!FNIC_FC_FRAME_TYPE_FC_GS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
			return -1;
		}
		return FNIC_FABRIC_GPN_FT_RSP;

	case FNIC_FRAME_TYPE_FABRIC_LOGO:
		return FNIC_FABRIC_LOGO_RSP;
	default:
		/* Drop the Rx frame and log/stats it */
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
					 "Solicited response: unknown OXID: 0x%x", oxid);
		return -1;
	}

	return -1;
}

void fnic_fdls_recv_frame(struct fnic_iport_s *iport, void *rx_frame,
						  int len, int fchdr_offset)
{
	struct fc_frame_header *fchdr;
	uint32_t s_id = 0;
	uint32_t d_id = 0;
	struct fnic *fnic = iport->fnic;
	int frame_type;

	fchdr = (struct fc_frame_header *) ((uint8_t *) rx_frame + fchdr_offset);
	s_id = ntoh24(fchdr->fh_s_id);
	d_id = ntoh24(fchdr->fh_d_id);

	fnic_debug_dump_fc_frame(fnic, fchdr, len, "Incoming");

	frame_type =
		fnic_fdls_validate_and_get_frame_type(iport, fchdr);

	/*if we are in flogo drop everything else */
	if (iport->fabric.state == FDLS_STATE_FABRIC_LOGO &&
		frame_type != FNIC_FABRIC_LOGO_RSP)
		return;

	switch (frame_type) {
	case FNIC_FABRIC_FLOGI_RSP:
		fdls_process_flogi_rsp(iport, fchdr, rx_frame);
		break;
	case FNIC_FABRIC_PLOGI_RSP:
		fdls_process_fabric_plogi_rsp(iport, fchdr);
		break;
	case FNIC_FABRIC_RPN_RSP:
		fdls_process_rpn_id_rsp(iport, fchdr);
		break;
	case FNIC_FABRIC_RFT_RSP:
		fdls_process_rft_id_rsp(iport, fchdr);
		break;
	case FNIC_FABRIC_RFF_RSP:
		fdls_process_rff_id_rsp(iport, fchdr);
		break;
	case FNIC_FABRIC_SCR_RSP:
		fdls_process_scr_rsp(iport, fchdr);
		break;
	case FNIC_FABRIC_GPN_FT_RSP:
		fdls_process_gpn_ft_rsp(iport, fchdr, len);
		break;
	case FNIC_FABRIC_LOGO_RSP:
		fdls_process_fabric_logo_rsp(iport, fchdr);
		break;
	case FNIC_FABRIC_BLS_ABTS_RSP:
			fdls_process_fabric_abts_rsp(iport, fchdr);
		break;
	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "s_id: 0x%x d_did: 0x%x", s_id, d_id);
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host, fnic->fnic_num,
			 "Received unknown FCoE frame of len: %d. Dropping frame", len);
		break;
	}
}
