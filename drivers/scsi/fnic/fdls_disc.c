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

/* FNIC FDMI Register HBA Macros */
#define FNIC_FDMI_NUM_PORTS 1
#define FNIC_FDMI_NUM_HBA_ATTRS 9
#define FNIC_FDMI_TYPE_NODE_NAME	0X1
#define FNIC_FDMI_TYPE_MANUFACTURER	0X2
#define FNIC_FDMI_MANUFACTURER		"Cisco Systems"
#define FNIC_FDMI_TYPE_SERIAL_NUMBER	0X3
#define FNIC_FDMI_TYPE_MODEL		0X4
#define FNIC_FDMI_TYPE_MODEL_DES	0X5
#define FNIC_FDMI_MODEL_DESCRIPTION	"Cisco Virtual Interface Card"
#define FNIC_FDMI_TYPE_HARDWARE_VERSION	0X6
#define FNIC_FDMI_TYPE_DRIVER_VERSION	0X7
#define FNIC_FDMI_TYPE_ROM_VERSION	0X8
#define FNIC_FDMI_TYPE_FIRMWARE_VERSION	0X9
#define FNIC_FDMI_NN_LEN 8
#define FNIC_FDMI_MANU_LEN 20
#define FNIC_FDMI_SERIAL_LEN 16
#define FNIC_FDMI_MODEL_LEN 12
#define FNIC_FDMI_MODEL_DES_LEN 56
#define FNIC_FDMI_HW_VER_LEN 16
#define FNIC_FDMI_DR_VER_LEN 28
#define FNIC_FDMI_ROM_VER_LEN 8
#define FNIC_FDMI_FW_VER_LEN 16

/* FNIC FDMI Register PA Macros */
#define FNIC_FDMI_TYPE_FC4_TYPES	0X1
#define FNIC_FDMI_TYPE_SUPPORTED_SPEEDS 0X2
#define FNIC_FDMI_TYPE_CURRENT_SPEED	0X3
#define FNIC_FDMI_TYPE_MAX_FRAME_SIZE	0X4
#define FNIC_FDMI_TYPE_OS_NAME		0X5
#define FNIC_FDMI_TYPE_HOST_NAME	0X6
#define FNIC_FDMI_NUM_PORT_ATTRS 6
#define FNIC_FDMI_FC4_LEN 32
#define FNIC_FDMI_SUPP_SPEED_LEN 4
#define FNIC_FDMI_CUR_SPEED_LEN 4
#define FNIC_FDMI_MFS_LEN 4
#define FNIC_FDMI_MFS 0x800
#define FNIC_FDMI_OS_NAME_LEN 16
#define FNIC_FDMI_HN_LEN 24

#define FDLS_FDMI_PLOGI_PENDING 0x1
#define FDLS_FDMI_REG_HBA_PENDING 0x2
#define FDLS_FDMI_RPA_PENDING 0x4
#define FDLS_FDMI_ABORT_PENDING 0x8
#define FDLS_FDMI_MAX_RETRY 3

#define RETRIES_EXHAUSTED(iport)      \
	(iport->fabric.retry_counter == FABRIC_LOGO_MAX_RETRY)

#define FNIC_TPORT_MAX_NEXUS_RESTART (8)

#define SCHEDULE_OXID_FREE_RETRY_TIME (300)

/* Private Functions */
static void fdls_fdmi_register_hba(struct fnic_iport_s *iport);
static void fdls_fdmi_register_pa(struct fnic_iport_s *iport);
static void fdls_send_rpn_id(struct fnic_iport_s *iport);
static void fdls_process_flogi_rsp(struct fnic_iport_s *iport,
				   struct fc_frame_header *fchdr,
				   void *rx_frame);
static void fnic_fdls_start_plogi(struct fnic_iport_s *iport);
static void fnic_fdls_start_flogi(struct fnic_iport_s *iport);
static struct fnic_tport_s *fdls_create_tport(struct fnic_iport_s *iport,
					  uint32_t fcid,
					  uint64_t wwpn);
static void fdls_target_restart_nexus(struct fnic_tport_s *tport);
static void fdls_start_tport_timer(struct fnic_iport_s *iport,
					struct fnic_tport_s *tport, int timeout);
static void fdls_tport_timer_callback(struct timer_list *t);
static void fdls_send_fdmi_plogi(struct fnic_iport_s *iport);
static void fdls_start_fabric_timer(struct fnic_iport_s *iport,
			int timeout);
static void fdls_init_plogi_frame(uint8_t *frame, struct fnic_iport_s *iport);
static void fdls_init_els_acc_frame(uint8_t *frame, struct fnic_iport_s *iport);
static void fdls_init_els_rjt_frame(uint8_t *frame, struct fnic_iport_s *iport);
static void fdls_init_logo_frame(uint8_t *frame, struct fnic_iport_s *iport);
static void fdls_init_fabric_abts_frame(uint8_t *frame,
						struct fnic_iport_s *iport);

uint8_t *fdls_alloc_frame(struct fnic_iport_s *iport)
{
	struct fnic *fnic = iport->fnic;
	uint8_t *frame = NULL;

	frame = mempool_alloc(fnic->frame_pool, GFP_ATOMIC);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Alloc oxid: all oxid slots are busy iport state:%d\n",
			iport->state);
		return FNIC_UNASSIGNED_OXID;
	}

	WARN_ON(test_and_set_bit(idx, oxid_pool->bitmap));
	oxid_pool->next_idx = (idx + 1) % FNIC_OXID_POOL_SZ;	/* cycle through the bitmap */

	oxid = FNIC_OXID_ENCODE(idx, oxid_frame_type);
	*active_oxid = oxid;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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

		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		"Schedule oxid free. oxid: 0x%x\n", *active_oxid);

	*active_oxid = FNIC_UNASSIGNED_OXID;

	reclaim_entry = (struct reclaim_entry_s *)
		kzalloc(sizeof(struct reclaim_entry_s), GFP_ATOMIC);

	if (!reclaim_entry) {
		FNIC_FCS_DBG(KERN_WARNING, fnic->host, fnic->fnic_num,
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
	unsigned long flags;
	int idx;

	for_each_set_bit(idx, oxid_pool->pending_schedule_free, FNIC_OXID_POOL_SZ) {

		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Schedule oxid free. oxid idx: %d\n", idx);

		reclaim_entry = kzalloc(sizeof(*reclaim_entry), GFP_KERNEL);
		if (!reclaim_entry) {
			schedule_delayed_work(&oxid_pool->schedule_oxid_free_retry,
				msecs_to_jiffies(SCHEDULE_OXID_FREE_RETRY_TIME));
			return;
		}

		clear_bit(idx, oxid_pool->pending_schedule_free);
		reclaim_entry->oxid_idx = idx;
		reclaim_entry->expires = round_jiffies(jiffies + delay_j);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		list_add_tail(&reclaim_entry->links, &oxid_pool->oxid_reclaim_list);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		schedule_delayed_work(&oxid_pool->oxid_reclaim_work, delay_j);
	}
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

static void fdls_reset_oxid_pool(struct fnic_iport_s *iport)
{
	struct fnic_oxid_pool_s *oxid_pool = &iport->oxid_pool;

	oxid_pool->next_idx = 0;
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "fabric timer is %d ", timeout);
}

static void
fdls_start_tport_timer(struct fnic_iport_s *iport,
					   struct fnic_tport_s *tport, int timeout)
{
	u64 fabric_tov;
	struct fnic *fnic = iport->fnic;

	if (tport->timer_pending) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "tport fcid 0x%x: Canceling disc timer\n",
					 tport->fcid);
		fnic_del_tport_timer_sync(fnic, tport);
		tport->timer_pending = 0;
	}

	if (!(tport->flags & FNIC_FDLS_TGT_ABORT_ISSUED))
		tport->retry_counter++;

	fabric_tov = jiffies + msecs_to_jiffies(timeout);
	mod_timer(&tport->retry_timer, round_jiffies(fabric_tov));
	tport->timer_pending = 1;
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

static void fdls_init_els_acc_frame(uint8_t *frame,
		struct fnic_iport_s *iport)
{
	struct fc_std_els_acc_rsp *pels_acc;
	uint8_t s_id[3];

	pels_acc = (struct fc_std_els_acc_rsp *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*pels_acc = (struct fc_std_els_acc_rsp) {
		.fchdr = {.fh_r_ctl = FC_RCTL_ELS_REP,
			  .fh_type = FC_TYPE_ELS, .fh_f_ctl = {FNIC_ELS_REP_FCTL, 0, 0}},
		.acc.la_cmd = ELS_LS_ACC,
	};

	hton24(s_id, iport->fcid);
	FNIC_STD_SET_S_ID(pels_acc->fchdr, s_id);
	FNIC_STD_SET_RX_ID(pels_acc->fchdr, FNIC_UNASSIGNED_RXID);
}

static void fdls_init_els_rjt_frame(uint8_t *frame,
		struct fnic_iport_s *iport)
{
	struct fc_std_els_rjt_rsp *pels_rjt;

	pels_rjt = (struct fc_std_els_rjt_rsp *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*pels_rjt = (struct fc_std_els_rjt_rsp) {
		.fchdr = {.fh_r_ctl = FC_RCTL_ELS_REP, .fh_type = FC_TYPE_ELS,
			  .fh_f_ctl = {FNIC_ELS_REP_FCTL, 0, 0}},
		.rej.er_cmd = ELS_LS_RJT,
	};

	FNIC_STD_SET_RX_ID(pels_rjt->fchdr, FNIC_UNASSIGNED_RXID);
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

static void
fdls_send_rscn_resp(struct fnic_iport_s *iport,
		    struct fc_frame_header *rscn_fchdr)
{
	uint8_t *frame;
	struct fc_std_els_acc_rsp *pels_acc;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_els_acc_rsp);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send RSCN response");
		return;
	}

	pels_acc = (struct fc_std_els_acc_rsp *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_els_acc_frame(frame, iport);

	FNIC_STD_SET_D_ID(pels_acc->fchdr, rscn_fchdr->fh_s_id);

	oxid = FNIC_STD_GET_OX_ID(rscn_fchdr);
	FNIC_STD_SET_OX_ID(pels_acc->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send RSCN response with oxid: 0x%x",
		 iport->fcid, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
}

static void
fdls_send_logo_resp(struct fnic_iport_s *iport,
		    struct fc_frame_header *req_fchdr)
{
	uint8_t *frame;
	struct fc_std_els_acc_rsp *plogo_resp;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_els_acc_rsp);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send LOGO response");
		return;
	}

	plogo_resp = (struct fc_std_els_acc_rsp *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_els_acc_frame(frame, iport);

	FNIC_STD_SET_D_ID(plogo_resp->fchdr, req_fchdr->fh_s_id);

	oxid = FNIC_STD_GET_OX_ID(req_fchdr);
	FNIC_STD_SET_OX_ID(plogo_resp->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send LOGO response with oxid: 0x%x",
		 iport->fcid, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
}

void
fdls_send_tport_abts(struct fnic_iport_s *iport,
					 struct fnic_tport_s *tport)
{
	uint8_t *frame;
	uint8_t s_id[3];
	uint8_t d_id[3];
	struct fnic *fnic = iport->fnic;
	struct fc_frame_header *ptport_abts;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_frame_header);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send tport ABTS");
		return;
	}

	ptport_abts = (struct fc_frame_header *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*ptport_abts = (struct fc_frame_header) {
		.fh_r_ctl = FC_RCTL_BA_ABTS,	/* ABTS */
		.fh_cs_ctl = 0x00, .fh_type = FC_TYPE_BLS,
		.fh_f_ctl = {FNIC_REQ_ABTS_FCTL, 0, 0}, .fh_seq_id = 0x00,
		.fh_df_ctl = 0x00, .fh_seq_cnt = 0x0000,
		.fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID),
		.fh_parm_offset = 0x00000000,	/* bit:0 = 0 Abort a exchange */
	};

	hton24(s_id, iport->fcid);
	hton24(d_id, tport->fcid);
	FNIC_STD_SET_S_ID(*ptport_abts, s_id);
	FNIC_STD_SET_D_ID(*ptport_abts, d_id);
	tport->flags |= FNIC_FDLS_TGT_ABORT_ISSUED;

	FNIC_STD_SET_OX_ID(*ptport_abts, tport->active_oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS send tport abts: tport->state: %d ",
				 iport->fcid, tport->state);

	fnic_send_fcoe_frame(iport, frame, frame_size);

	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_tport_timer(iport, tport, 2 * iport->e_d_tov);
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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
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

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send fabric abts. iport->fabric.state: %d oxid: 0x%x",
		 iport->fcid, iport->fabric.state, oxid);

	iport->fabric.flags |= FNIC_FDLS_FABRIC_ABORT_ISSUED;

	fnic_send_fcoe_frame(iport, frame, frame_size);

	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
	iport->fabric.timer_pending = 1;
}

static void fdls_send_fdmi_abts(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	uint8_t d_id[3];
	struct fnic *fnic = iport->fnic;
	struct fc_frame_header *pfabric_abts;
	unsigned long fdmi_tov;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_frame_header);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send FDMI ABTS");
		return;
	}

	pfabric_abts = (struct fc_frame_header *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_fabric_abts_frame(frame, iport);

	hton24(d_id, FC_FID_MGMT_SERV);
	FNIC_STD_SET_D_ID(*pfabric_abts, d_id);

	if (iport->fabric.fdmi_pending & FDLS_FDMI_PLOGI_PENDING) {
		oxid = iport->active_oxid_fdmi_plogi;
		FNIC_STD_SET_OX_ID(*pfabric_abts, oxid);
		fnic_send_fcoe_frame(iport, frame, frame_size);
	} else {
		if (iport->fabric.fdmi_pending & FDLS_FDMI_REG_HBA_PENDING) {
			oxid = iport->active_oxid_fdmi_rhba;
			FNIC_STD_SET_OX_ID(*pfabric_abts, oxid);
			fnic_send_fcoe_frame(iport, frame, frame_size);
		}
		if (iport->fabric.fdmi_pending & FDLS_FDMI_RPA_PENDING) {
			oxid = iport->active_oxid_fdmi_rpa;
			FNIC_STD_SET_OX_ID(*pfabric_abts, oxid);
			fnic_send_fcoe_frame(iport, frame, frame_size);
		}
	}

	fdmi_tov = jiffies + msecs_to_jiffies(2 * iport->e_d_tov);
	mod_timer(&iport->fabric.fdmi_timer, round_jiffies(fdmi_tov));
	iport->fabric.fdmi_pending |= FDLS_FDMI_ABORT_PENDING;
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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send FLOGI",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pflogi->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send fabric FLOGI with oxid: 0x%x", iport->fcid,
		 oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
	atomic64_inc(&iport->iport_stats.fabric_flogi_sent);
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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to send PLOGI");
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	pplogi = (struct fc_std_flogi *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_plogi_frame(frame, iport);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_PLOGI,
		&iport->active_oxid_fabric_req);
	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send fabric PLOGI",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pplogi->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send fabric PLOGI with oxid: 0x%x", iport->fcid,
		 oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
	atomic64_inc(&iport->iport_stats.fabric_plogi_sent);

err_out:
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

static void fdls_send_fdmi_plogi(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_flogi *pplogi;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_flogi);
	uint8_t d_id[3];
	u64 fdmi_tov;

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to send FDMI PLOGI");
		goto err_out;
	}

	pplogi = (struct fc_std_flogi *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_plogi_frame(frame, iport);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FDMI_PLOGI,
		&iport->active_oxid_fdmi_plogi);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "0x%x: Failed to allocate OXID to send FDMI PLOGI",
			     iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pplogi->fchdr, oxid);

	hton24(d_id, FC_FID_MGMT_SERV);
	FNIC_STD_SET_D_ID(pplogi->fchdr, d_id);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: FDLS send FDMI PLOGI with oxid: 0x%x",
		     iport->fcid, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);

err_out:
	fdmi_tov = jiffies + msecs_to_jiffies(2 * iport->e_d_tov);
	mod_timer(&iport->fabric.fdmi_timer, round_jiffies(fdmi_tov));
	iport->fabric.fdmi_pending = FDLS_FDMI_PLOGI_PENDING;
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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send RPN_ID",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(prpn_id->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send SCR",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pscr->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send SCR with oxid: 0x%x", iport->fcid,
		 oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
	atomic64_inc(&iport->iport_stats.fabric_scr_sent);

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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send GPN FT",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		iport->fabric.flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pgpn_ft->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send GPN FT with oxid: 0x%x", iport->fcid,
		 oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);

err_out:
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
	fdls_set_state((&iport->fabric), fdls_state);
}

static void
fdls_send_tgt_adisc(struct fnic_iport_s *iport, struct fnic_tport_s *tport)
{
	uint8_t *frame;
	struct fc_std_els_adisc *padisc;
	uint8_t s_id[3];
	uint8_t d_id[3];
	uint16_t oxid;
	struct fnic *fnic = iport->fnic;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_els_adisc);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send TGT ADISC");
		tport->flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	padisc = (struct fc_std_els_adisc *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);

	hton24(s_id, iport->fcid);
	hton24(d_id, tport->fcid);
	memcpy(padisc->els.adisc_port_id, s_id, 3);
	FNIC_STD_SET_S_ID(padisc->fchdr, s_id);
	FNIC_STD_SET_D_ID(padisc->fchdr, d_id);

	FNIC_STD_SET_F_CTL(padisc->fchdr, FNIC_ELS_REQ_FCTL << 16);
	FNIC_STD_SET_R_CTL(padisc->fchdr, FC_RCTL_ELS_REQ);
	FNIC_STD_SET_TYPE(padisc->fchdr, FC_TYPE_ELS);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_TGT_ADISC, &tport->active_oxid);
	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "0x%x: Failed to allocate OXID to send TGT ADISC",
					 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		tport->flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(padisc->fchdr, oxid);
	FNIC_STD_SET_RX_ID(padisc->fchdr, FNIC_UNASSIGNED_RXID);

	tport->flags &= ~FNIC_FDLS_TGT_ABORT_ISSUED;

	FNIC_STD_SET_NPORT_NAME(&padisc->els.adisc_wwpn,
				iport->wwpn);
	FNIC_STD_SET_NODE_NAME(&padisc->els.adisc_wwnn,
			iport->wwnn);

	padisc->els.adisc_cmd = ELS_ADISC;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS send ADISC to tgt fcid: 0x%x",
				 iport->fcid, tport->fcid);

	atomic64_inc(&iport->iport_stats.tport_adisc_sent);

	fnic_send_fcoe_frame(iport, frame, frame_size);

err_out:
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_tport_timer(iport, tport, 2 * iport->e_d_tov);
}

bool fdls_delete_tport(struct fnic_iport_s *iport, struct fnic_tport_s *tport)
{
	struct fnic_tport_event_s *tport_del_evt;
	struct fnic *fnic = iport->fnic;

	if ((tport->state == FDLS_TGT_STATE_OFFLINING)
	    || (tport->state == FDLS_TGT_STATE_OFFLINE)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "tport fcid 0x%x: tport state is offlining/offline\n",
			     tport->fcid);
		return false;
	}

	fdls_set_tport_state(tport, FDLS_TGT_STATE_OFFLINING);
	/*
	 * By setting this flag, the tport will not be seen in a look-up
	 * in an RSCN. Even if we move to multithreaded model, this tport
	 * will be destroyed and a new RSCN will have to create a new one
	 */
	tport->flags |= FNIC_FDLS_TPORT_TERMINATING;

	if (tport->timer_pending) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "tport fcid 0x%x: Canceling disc timer\n",
					 tport->fcid);
		fnic_del_tport_timer_sync(fnic, tport);
		tport->timer_pending = 0;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	fnic_rport_exch_reset(iport->fnic, tport->fcid);
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	if (tport->flags & FNIC_FDLS_SCSI_REGISTERED) {
		tport_del_evt =
			kzalloc(sizeof(struct fnic_tport_event_s), GFP_ATOMIC);
		if (!tport_del_evt) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Failed to allocate memory for tport fcid: 0x%0x\n",
				 tport->fcid);
			return false;
		}
		tport_del_evt->event = TGT_EV_RPORT_DEL;
		tport_del_evt->arg1 = (void *) tport;
		list_add_tail(&tport_del_evt->links, &fnic->tport_event_list);
		queue_work(fnic_event_queue, &fnic->tport_work);
	} else {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "tport 0x%x not reg with scsi_transport. Freeing locally",
			 tport->fcid);
		list_del(&tport->links);
		kfree(tport);
	}
	return true;
}

static void
fdls_send_tgt_plogi(struct fnic_iport_s *iport, struct fnic_tport_s *tport)
{
	uint8_t *frame;
	struct fc_std_flogi *pplogi;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_flogi);
	uint8_t d_id[3];
	uint32_t timeout;

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send TGT PLOGI");
		tport->flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	pplogi = (struct fc_std_flogi *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_plogi_frame(frame, iport);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_TGT_PLOGI, &tport->active_oxid);
	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: Failed to allocate oxid to send PLOGI to fcid: 0x%x",
				 iport->fcid, tport->fcid);
		mempool_free(frame, fnic->frame_pool);
		tport->flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}
	FNIC_STD_SET_OX_ID(pplogi->fchdr, oxid);

	tport->flags &= ~FNIC_FDLS_TGT_ABORT_ISSUED;

	hton24(d_id, tport->fcid);
	FNIC_STD_SET_D_ID(pplogi->fchdr, d_id);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS send tgt PLOGI to tgt: 0x%x with oxid: 0x%x",
				 iport->fcid, tport->fcid, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
	atomic64_inc(&iport->iport_stats.tport_plogi_sent);

err_out:
	timeout = max(2 * iport->e_d_tov, iport->plogi_timeout);
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_tport_timer(iport, tport, timeout);
}

static uint16_t
fnic_fc_plogi_rsp_rdf(struct fnic_iport_s *iport,
		      struct fc_std_flogi *plogi_rsp)
{
	uint16_t b2b_rdf_size =
	    be16_to_cpu(FNIC_LOGI_RDF_SIZE(plogi_rsp->els));
	uint16_t spc3_rdf_size =
	    be16_to_cpu(plogi_rsp->els.fl_cssp[2].cp_rdfs) & FNIC_FC_C3_RDF;
	struct fnic *fnic = iport->fnic;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "MFS: b2b_rdf_size: 0x%x spc3_rdf_size: 0x%x",
			 b2b_rdf_size, spc3_rdf_size);

	return min(b2b_rdf_size, spc3_rdf_size);
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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send RFT",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(prft_id->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send RFT with oxid: 0x%x", iport->fcid,
		 oxid);

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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "0x%x: Failed to allocate OXID to send RFF",
				 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(prff_id->fchdr, oxid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send RFF with oxid: 0x%x", iport->fcid,
		 oxid);

	prff_id->rff_id.fr_type = FC_TYPE_FCP;

	fnic_send_fcoe_frame(iport, frame, frame_size);

	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

static void
fdls_send_tgt_prli(struct fnic_iport_s *iport, struct fnic_tport_s *tport)
{
	uint8_t *frame;
	struct fc_std_els_prli *pprli;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_els_prli);
	uint8_t s_id[3];
	uint8_t d_id[3];
	uint32_t timeout;

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send TGT PRLI");
		tport->flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	pprli = (struct fc_std_els_prli *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*pprli = (struct fc_std_els_prli) {
		.fchdr = {.fh_r_ctl = FC_RCTL_ELS_REQ, .fh_type = FC_TYPE_ELS,
			  .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
			  .fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)},
		.els_prli = {.prli_cmd = ELS_PRLI,
			     .prli_spp_len = 16,
			     .prli_len = cpu_to_be16(0x14)},
		.sp = {.spp_type = 0x08, .spp_flags = 0x0020,
		       .spp_params = cpu_to_be32(0xA2)}
	};

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_TGT_PRLI, &tport->active_oxid);
	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"0x%x: Failed to allocate OXID to send TGT PRLI to 0x%x",
			iport->fcid, tport->fcid);
		mempool_free(frame, fnic->frame_pool);
		tport->flags |= FNIC_FDLS_RETRY_FRAME;
		goto err_out;
	}

	tport->flags &= ~FNIC_FDLS_TGT_ABORT_ISSUED;

	hton24(s_id, iport->fcid);
	hton24(d_id, tport->fcid);

	FNIC_STD_SET_OX_ID(pprli->fchdr, oxid);
	FNIC_STD_SET_S_ID(pprli->fchdr, s_id);
	FNIC_STD_SET_D_ID(pprli->fchdr, d_id);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"0x%x: FDLS send PRLI to tgt: 0x%x with oxid: 0x%x",
			iport->fcid, tport->fcid, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
	atomic64_inc(&iport->iport_stats.tport_prli_sent);

err_out:
	timeout = max(2 * iport->e_d_tov, iport->plogi_timeout);
	/* Even if fnic_send_fcoe_frame() fails we want to retry after timeout */
	fdls_start_tport_timer(iport, tport, timeout);
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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to send fabric LOGO");
		return;
	}

	plogo = (struct fc_std_logo *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_logo_frame(frame, iport);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_LOGO,
		&iport->active_oxid_fabric_req);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send fabric LOGO",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(plogo->fchdr, oxid);

	hton24(d_id, FC_FID_FLOGI);
	FNIC_STD_SET_D_ID(plogo->fchdr, d_id);

	iport->fabric.flags &= ~FNIC_FDLS_FABRIC_ABORT_ISSUED;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: FDLS send fabric LOGO with oxid: 0x%x",
		     iport->fcid, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);

	fdls_start_fabric_timer(iport, 2 * iport->e_d_tov);
}

/**
 * fdls_tgt_logout - Send plogo to the remote port
 * @iport: Handle to fnic iport
 * @tport: Handle to remote port
 *
 * This function does not change or check the fabric/tport state.
 * It the caller's responsibility to set the appropriate tport/fabric
 * state when this is called. Normally that is fdls_tgt_state_plogo.
 * This could be used to send plogo to nameserver process
 * also not just target processes
 */
void fdls_tgt_logout(struct fnic_iport_s *iport, struct fnic_tport_s *tport)
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
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to send fabric LOGO");
		return;
	}

	plogo = (struct fc_std_logo *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_logo_frame(frame, iport);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_TGT_LOGO, &tport->active_oxid);
	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "0x%x: Failed to allocate OXID to send tgt LOGO",
		     iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(plogo->fchdr, oxid);

	hton24(d_id, tport->fcid);
	FNIC_STD_SET_D_ID(plogo->fchdr, d_id);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send tgt LOGO with oxid: 0x%x",
		 iport->fcid, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);

	atomic64_inc(&iport->iport_stats.tport_logo_sent);
}

static void fdls_tgt_discovery_start(struct fnic_iport_s *iport)
{
	struct fnic_tport_s *tport, *next;
	u32 old_link_down_cnt = iport->fnic->link_down_cnt;
	struct fnic *fnic = iport->fnic;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: Starting FDLS target discovery", iport->fcid);

	list_for_each_entry_safe(tport, next, &iport->tport_list, links) {
		if ((old_link_down_cnt != iport->fnic->link_down_cnt)
			|| (iport->state != FNIC_IPORT_STATE_READY)) {
			break;
		}
		/* if we marked the tport as deleted due to GPN_FT
		 * We should not send ADISC anymore
		 */
		if ((tport->state == FDLS_TGT_STATE_OFFLINING) ||
			(tport->state == FDLS_TGT_STATE_OFFLINE))
			continue;

		/* For tports which have received RSCN */
		if (tport->flags & FNIC_FDLS_TPORT_SEND_ADISC) {
			tport->retry_counter = 0;
			fdls_set_tport_state(tport, FDLS_TGT_STATE_ADISC);
			tport->flags &= ~FNIC_FDLS_TPORT_SEND_ADISC;
			fdls_send_tgt_adisc(iport, tport);
			continue;
		}
		if (fdls_get_tport_state(tport) != FDLS_TGT_STATE_INIT) {
			/* Not a new port, skip  */
			continue;
		}
		tport->retry_counter = 0;
		fdls_set_tport_state(tport, FDLS_TGT_STATE_PLOGI);
		fdls_send_tgt_plogi(iport, tport);
	}
	fdls_set_state((&iport->fabric), FDLS_STATE_TGT_DISCOVERY);
}

/*
 * Function to restart the IT nexus if we received any out of
 * sequence PLOGI/PRLI  response from the target.
 * The memory for the new tport structure is allocated
 * inside fdls_create_tport and added to the iport's tport list.
 * This will get freed later during tport_offline/linkdown
 * or module unload. The new_tport pointer will go out of scope
 * safely since the memory it is
 * pointing to it will be freed later
 */
static void fdls_target_restart_nexus(struct fnic_tport_s *tport)
{
	struct fnic_iport_s *iport = tport->iport;
	struct fnic_tport_s *new_tport = NULL;
	uint32_t fcid;
	uint64_t wwpn;
	int nexus_restart_count;
	struct fnic *fnic = iport->fnic;
	bool retval = true;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "tport fcid: 0x%x state: %d restart_count: %d",
				 tport->fcid, tport->state, tport->nexus_restart_count);

	fcid = tport->fcid;
	wwpn = tport->wwpn;
	nexus_restart_count = tport->nexus_restart_count;

	retval = fdls_delete_tport(iport, tport);
	if (retval != true) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			     "Error deleting tport: 0x%x", fcid);
		return;
	}

	if (nexus_restart_count >= FNIC_TPORT_MAX_NEXUS_RESTART) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "Exceeded nexus restart retries tport: 0x%x",
			     fcid);
		return;
	}

	/*
	 * Allocate memory for the new tport and add it to
	 * iport's tport list.
	 * This memory will be freed during tport_offline/linkdown
	 * or module unload. The pointer new_tport is safe to go
	 * out of scope when this function returns, since the memory
	 * it is pointing to is guaranteed to be freed later
	 * as mentioned above.
	 */
	new_tport = fdls_create_tport(iport, fcid, wwpn);
	if (!new_tport) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Error creating new tport: 0x%x", fcid);
		return;
	}

	new_tport->nexus_restart_count = nexus_restart_count + 1;
	fdls_send_tgt_plogi(iport, new_tport);
	fdls_set_tport_state(new_tport, FDLS_TGT_STATE_PLOGI);
}

struct fnic_tport_s *fnic_find_tport_by_fcid(struct fnic_iport_s *iport,
									 uint32_t fcid)
{
	struct fnic_tport_s *tport, *next;

	list_for_each_entry_safe(tport, next, &(iport->tport_list), links) {
		if ((tport->fcid == fcid)
			&& !(tport->flags & FNIC_FDLS_TPORT_TERMINATING))
			return tport;
	}
	return NULL;
}

static struct fnic_tport_s *fdls_create_tport(struct fnic_iport_s *iport,
								  uint32_t fcid, uint64_t wwpn)
{
	struct fnic_tport_s *tport;
	struct fnic *fnic = iport->fnic;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "FDLS create tport: fcid: 0x%x wwpn: 0x%llx", fcid, wwpn);

	tport = kzalloc(sizeof(struct fnic_tport_s), GFP_ATOMIC);
	if (!tport) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Memory allocation failure while creating tport: 0x%x\n",
			 fcid);
		return NULL;
	}

	tport->max_payload_size = FNIC_FCOE_MAX_FRAME_SZ;
	tport->r_a_tov = FC_DEF_R_A_TOV;
	tport->e_d_tov = FC_DEF_E_D_TOV;
	tport->fcid = fcid;
	tport->wwpn = wwpn;
	tport->iport = iport;

	FNIC_FCS_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				 "Need to setup tport timer callback");

	timer_setup(&tport->retry_timer, fdls_tport_timer_callback, 0);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Added tport 0x%x", tport->fcid);
	fdls_set_tport_state(tport, FDLS_TGT_STATE_INIT);
	list_add_tail(&tport->links, &iport->tport_list);
	atomic_set(&tport->in_flight, 0);
	return tport;
}

struct fnic_tport_s *fnic_find_tport_by_wwpn(struct fnic_iport_s *iport,
									 uint64_t wwpn)
{
	struct fnic_tport_s *tport, *next;

	list_for_each_entry_safe(tport, next, &(iport->tport_list), links) {
		if ((tport->wwpn == wwpn)
			&& !(tport->flags & FNIC_FDLS_TPORT_TERMINATING))
			return tport;
	}
	return NULL;
}

static void
fnic_fdmi_attr_set(void *attr_start, u16 type, u16 len,
		void *data, u32 *off)
{
	u16 size = len + FC_FDMI_ATTR_ENTRY_HEADER_LEN;
	struct fc_fdmi_attr_entry *fdmi_attr = (struct fc_fdmi_attr_entry *)
		((u8 *)attr_start + *off);

	put_unaligned_be16(type, &fdmi_attr->type);
	put_unaligned_be16(size, &fdmi_attr->len);
	memcpy(fdmi_attr->value, data, len);
	*off += size;
}

static void fdls_fdmi_register_hba(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_fdmi_rhba *prhba;
	struct fc_fdmi_attr_entry *fdmi_attr;
	uint8_t fcid[3];
	int err;
	struct fnic *fnic = iport->fnic;
	struct vnic_devcmd_fw_info *fw_info = NULL;
	uint16_t oxid;
	u32 attr_off_bytes, len;
	u8 data[64];
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET;

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to send FDMI RHBA");
		return;
	}

	prhba = (struct fc_std_fdmi_rhba *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*prhba = (struct fc_std_fdmi_rhba) {
		.fchdr = {
			.fh_r_ctl = FC_RCTL_DD_UNSOL_CTL,
			.fh_d_id = {0xFF, 0XFF, 0XFA},
			.fh_type = FC_TYPE_CT,
			.fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
			.fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)
		},
		.fc_std_ct_hdr = {
			.ct_rev = FC_CT_REV, .ct_fs_type = FC_FST_MGMT,
			.ct_fs_subtype = FC_FDMI_SUBTYPE,
			.ct_cmd = cpu_to_be16(FC_FDMI_RHBA)
		},
	};

	hton24(fcid, iport->fcid);
	FNIC_STD_SET_S_ID(prhba->fchdr, fcid);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FDMI_RHBA,
		&iport->active_oxid_fdmi_rhba);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "0x%x: Failed to allocate OXID to send FDMI RHBA",
		     iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(prhba->fchdr, oxid);

	put_unaligned_be64(iport->wwpn, &prhba->rhba.hbaid.id);
	put_unaligned_be32(FNIC_FDMI_NUM_PORTS, &prhba->rhba.port.numport);
	put_unaligned_be64(iport->wwpn, &prhba->rhba.port.port[0].portname);
	put_unaligned_be32(FNIC_FDMI_NUM_HBA_ATTRS,
			&prhba->rhba.hba_attrs.numattrs);

	fdmi_attr = prhba->rhba.hba_attrs.attr;
	attr_off_bytes = 0;

	put_unaligned_be64(iport->wwnn, data);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_NODE_NAME,
		FNIC_FDMI_NN_LEN, data, &attr_off_bytes);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"NN set, off=%d", attr_off_bytes);

	strscpy_pad(data, FNIC_FDMI_MANUFACTURER, FNIC_FDMI_MANU_LEN);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_MANUFACTURER,
		FNIC_FDMI_MANU_LEN, data, &attr_off_bytes);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"MFG set <%s>, off=%d", data, attr_off_bytes);

	err = vnic_dev_fw_info(fnic->vdev, &fw_info);
	if (!err) {
		strscpy_pad(data, fw_info->hw_serial_number,
				FNIC_FDMI_SERIAL_LEN);
		fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_SERIAL_NUMBER,
			FNIC_FDMI_SERIAL_LEN, data, &attr_off_bytes);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"SERIAL set <%s>, off=%d", data, attr_off_bytes);

	}

	if (fnic->subsys_desc_len >= FNIC_FDMI_MODEL_LEN)
		fnic->subsys_desc_len = FNIC_FDMI_MODEL_LEN - 1;
	strscpy_pad(data, fnic->subsys_desc, FNIC_FDMI_MODEL_LEN);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_MODEL, FNIC_FDMI_MODEL_LEN,
		data, &attr_off_bytes);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"MODEL set <%s>, off=%d", data, attr_off_bytes);

	strscpy_pad(data, FNIC_FDMI_MODEL_DESCRIPTION, FNIC_FDMI_MODEL_DES_LEN);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_MODEL_DES,
		FNIC_FDMI_MODEL_DES_LEN, data, &attr_off_bytes);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"MODEL_DESC set <%s>, off=%d", data, attr_off_bytes);

	if (!err) {
		strscpy_pad(data, fw_info->hw_version, FNIC_FDMI_HW_VER_LEN);
		fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_HARDWARE_VERSION,
			FNIC_FDMI_HW_VER_LEN, data, &attr_off_bytes);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"HW_VER set <%s>, off=%d", data, attr_off_bytes);

	}

	strscpy_pad(data, DRV_VERSION, FNIC_FDMI_DR_VER_LEN);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_DRIVER_VERSION,
		FNIC_FDMI_DR_VER_LEN, data, &attr_off_bytes);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"DRV_VER set <%s>, off=%d", data, attr_off_bytes);

	strscpy_pad(data, "N/A", FNIC_FDMI_ROM_VER_LEN);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_ROM_VERSION,
		FNIC_FDMI_ROM_VER_LEN, data, &attr_off_bytes);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"ROM_VER set <%s>, off=%d", data, attr_off_bytes);

	if (!err) {
		strscpy_pad(data, fw_info->fw_version, FNIC_FDMI_FW_VER_LEN);
		fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_FIRMWARE_VERSION,
			FNIC_FDMI_FW_VER_LEN, data, &attr_off_bytes);

		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"FW_VER set <%s>, off=%d", data, attr_off_bytes);
	}

	len = sizeof(struct fc_std_fdmi_rhba) + attr_off_bytes;
	frame_size += len;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send FDMI RHBA with oxid: 0x%x fs: %d", iport->fcid,
		 oxid, frame_size);

	fnic_send_fcoe_frame(iport, frame, frame_size);
	iport->fabric.fdmi_pending |= FDLS_FDMI_REG_HBA_PENDING;
}

static void fdls_fdmi_register_pa(struct fnic_iport_s *iport)
{
	uint8_t *frame;
	struct fc_std_fdmi_rpa *prpa;
	struct fc_fdmi_attr_entry *fdmi_attr;
	uint8_t fcid[3];
	struct fnic *fnic = iport->fnic;
	u32 port_speed_bm;
	u32 port_speed = vnic_dev_port_speed(fnic->vdev);
	uint16_t oxid;
	u32 attr_off_bytes, len;
	u8 tmp_data[16], data[64];
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET;

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to send FDMI RPA");
		return;
	}

	prpa = (struct fc_std_fdmi_rpa *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*prpa = (struct fc_std_fdmi_rpa) {
		.fchdr = {
			.fh_r_ctl = FC_RCTL_DD_UNSOL_CTL,
			.fh_d_id = {0xFF, 0xFF, 0xFA},
			.fh_type = FC_TYPE_CT,
			.fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
			.fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)
		},
		.fc_std_ct_hdr = {
			.ct_rev = FC_CT_REV, .ct_fs_type = FC_FST_MGMT,
			.ct_fs_subtype = FC_FDMI_SUBTYPE,
			.ct_cmd = cpu_to_be16(FC_FDMI_RPA)
		},
	};

	hton24(fcid, iport->fcid);
	FNIC_STD_SET_S_ID(prpa->fchdr, fcid);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FDMI_RPA,
		&iport->active_oxid_fdmi_rpa);

	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "0x%x: Failed to allocate OXID to send FDMI RPA",
			     iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(prpa->fchdr, oxid);

	put_unaligned_be64(iport->wwpn, &prpa->rpa.port.portname);
	put_unaligned_be32(FNIC_FDMI_NUM_PORT_ATTRS,
				&prpa->rpa.hba_attrs.numattrs);

	/* MDS does not support GIGE speed.
	 * Bit shift standard definitions from scsi_transport_fc.h to
	 * match FC spec.
	 */
	switch (port_speed) {
	case DCEM_PORTSPEED_10G:
	case DCEM_PORTSPEED_20G:
		/* There is no bit for 20G */
		port_speed_bm = FC_PORTSPEED_10GBIT << PORT_SPEED_BIT_14;
		break;
	case DCEM_PORTSPEED_25G:
		port_speed_bm = FC_PORTSPEED_25GBIT << PORT_SPEED_BIT_8;
		break;
	case DCEM_PORTSPEED_40G:
	case DCEM_PORTSPEED_4x10G:
		port_speed_bm = FC_PORTSPEED_40GBIT << PORT_SPEED_BIT_9;
		break;
	case DCEM_PORTSPEED_100G:
		port_speed_bm = FC_PORTSPEED_100GBIT << PORT_SPEED_BIT_8;
		break;
	default:
		port_speed_bm = FC_PORTSPEED_1GBIT << PORT_SPEED_BIT_15;
		break;
	}
	attr_off_bytes = 0;

	fdmi_attr = prpa->rpa.hba_attrs.attr;

	put_unaligned_be64(iport->wwnn, data);

	memset(data, 0, FNIC_FDMI_FC4_LEN);
	data[2] = 1;
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_FC4_TYPES,
		FNIC_FDMI_FC4_LEN, data, &attr_off_bytes);

	put_unaligned_be32(port_speed_bm, data);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_SUPPORTED_SPEEDS,
		FNIC_FDMI_SUPP_SPEED_LEN, data, &attr_off_bytes);

	put_unaligned_be32(port_speed_bm, data);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_CURRENT_SPEED,
		FNIC_FDMI_CUR_SPEED_LEN, data, &attr_off_bytes);

	put_unaligned_be32(FNIC_FDMI_MFS, data);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_MAX_FRAME_SIZE,
		FNIC_FDMI_MFS_LEN, data, &attr_off_bytes);

	snprintf(tmp_data, FNIC_FDMI_OS_NAME_LEN - 1, "host%d",
		 fnic->host->host_no);
	strscpy_pad(data, tmp_data, FNIC_FDMI_OS_NAME_LEN);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_OS_NAME,
		FNIC_FDMI_OS_NAME_LEN, data, &attr_off_bytes);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"OS name set <%s>, off=%d", data, attr_off_bytes);

	sprintf(fc_host_system_hostname(fnic->host), "%s", utsname()->nodename);
	strscpy_pad(data, fc_host_system_hostname(fnic->host),
					FNIC_FDMI_HN_LEN);
	fnic_fdmi_attr_set(fdmi_attr, FNIC_FDMI_TYPE_HOST_NAME,
		FNIC_FDMI_HN_LEN, data, &attr_off_bytes);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Host name set <%s>, off=%d", data, attr_off_bytes);

	len = sizeof(struct fc_std_fdmi_rpa) + attr_off_bytes;
	frame_size += len;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send FDMI RPA with oxid: 0x%x fs: %d", iport->fcid,
		 oxid, frame_size);

	fnic_send_fcoe_frame(iport, frame, frame_size);
	iport->fabric.fdmi_pending |= FDLS_FDMI_RPA_PENDING;
}

void fdls_fabric_timer_callback(struct timer_list *t)
{
	struct fnic_fdls_fabric_s *fabric = from_timer(fabric, t, retry_timer);
	struct fnic_iport_s *iport =
		container_of(fabric, struct fnic_iport_s, fabric);
	struct fnic *fnic = iport->fnic;
	unsigned long flags;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "ABTS timeout for fabric GPN_FT. Check name server: %p",
					 iport);
			}
		}
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

void fdls_fdmi_timer_callback(struct timer_list *t)
{
	struct fnic_fdls_fabric_s *fabric = from_timer(fabric, t, fdmi_timer);
	struct fnic_iport_s *iport =
		container_of(fabric, struct fnic_iport_s, fabric);
	struct fnic *fnic = iport->fnic;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		"fdmi timer callback : 0x%x\n", iport->fabric.fdmi_pending);

	if (!iport->fabric.fdmi_pending) {
		/* timer expired after fdmi responses received. */
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		"fdmi timer callback : 0x%x\n", iport->fabric.fdmi_pending);

	/* if not abort pending, send an abort */
	if (!(iport->fabric.fdmi_pending & FDLS_FDMI_ABORT_PENDING)) {
		fdls_send_fdmi_abts(iport);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		"fdmi timer callback : 0x%x\n", iport->fabric.fdmi_pending);

	/* ABTS pending for an active fdmi request that is pending.
	 * That means FDMI ABTS timed out
	 * Schedule to free the OXID after 2*r_a_tov and proceed
	 */
	if (iport->fabric.fdmi_pending & FDLS_FDMI_PLOGI_PENDING) {
		fdls_schedule_oxid_free(iport, &iport->active_oxid_fdmi_plogi);
	} else {
		if (iport->fabric.fdmi_pending & FDLS_FDMI_REG_HBA_PENDING)
			fdls_schedule_oxid_free(iport, &iport->active_oxid_fdmi_rhba);
		if (iport->fabric.fdmi_pending & FDLS_FDMI_RPA_PENDING)
			fdls_schedule_oxid_free(iport, &iport->active_oxid_fdmi_rpa);
	}
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		"fdmi timer callback : 0x%x\n", iport->fabric.fdmi_pending);

	iport->fabric.fdmi_pending = 0;
	/* If max retries not exhaused, start over from fdmi plogi */
	if (iport->fabric.fdmi_retry < FDLS_FDMI_MAX_RETRY) {
		iport->fabric.fdmi_retry++;
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "retry fdmi timer %d", iport->fabric.fdmi_retry);
		fdls_send_fdmi_plogi(iport);
	}
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		"fdmi timer callback : 0x%x\n", iport->fabric.fdmi_pending);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

static void fdls_send_delete_tport_msg(struct fnic_tport_s *tport)
{
	struct fnic_iport_s *iport = (struct fnic_iport_s *) tport->iport;
	struct fnic *fnic = iport->fnic;
	struct fnic_tport_event_s *tport_del_evt;

	tport_del_evt = kzalloc(sizeof(struct fnic_tport_event_s), GFP_ATOMIC);
	if (!tport_del_evt) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Failed to allocate memory for tport event fcid: 0x%x",
			 tport->fcid);
		return;
	}
	tport_del_evt->event = TGT_EV_TPORT_DELETE;
	tport_del_evt->arg1 = (void *) tport;
	list_add_tail(&tport_del_evt->links, &fnic->tport_event_list);
	queue_work(fnic_event_queue, &fnic->tport_work);
}

static void fdls_tport_timer_callback(struct timer_list *t)
{
	struct fnic_tport_s *tport = from_timer(tport, t, retry_timer);
	struct fnic_iport_s *iport = (struct fnic_iport_s *) tport->iport;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (!tport->timer_pending) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	if (iport->state != FNIC_IPORT_STATE_READY) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	if (tport->del_timer_inprogress) {
		tport->del_timer_inprogress = 0;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "tport_del_timer inprogress. Skip timer cb tport fcid: 0x%x\n",
			 tport->fcid);
		return;
	}

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "tport fcid: 0x%x timer pending: %d state: %d retry counter: %d",
		 tport->fcid, tport->timer_pending, tport->state,
		 tport->retry_counter);

	tport->timer_pending = 0;
	oxid = tport->active_oxid;

	/* We retry plogi/prli/adisc frames depending on the tport state */
	switch (tport->state) {
	case FDLS_TGT_STATE_PLOGI:
		/* PLOGI frame received a LS_RJT with busy, we retry from here */
		if ((tport->flags & FNIC_FDLS_RETRY_FRAME)
			&& (tport->retry_counter < iport->max_plogi_retries)) {
			tport->flags &= ~FNIC_FDLS_RETRY_FRAME;
			fdls_send_tgt_plogi(iport, tport);
		} else if (!(tport->flags & FNIC_FDLS_TGT_ABORT_ISSUED)) {
			/* Plogi frame has timed out, send abts */
			fdls_send_tport_abts(iport, tport);
		} else if (tport->retry_counter < iport->max_plogi_retries) {
			/*
			 * ABTS has timed out
			 */
			fdls_schedule_oxid_free(iport, &tport->active_oxid);
			fdls_send_tgt_plogi(iport, tport);
		} else {
			/* exceeded plogi retry count */
			fdls_schedule_oxid_free(iport, &tport->active_oxid);
			fdls_send_delete_tport_msg(tport);
		}
		break;
	case FDLS_TGT_STATE_PRLI:
		/* PRLI received a LS_RJT with busy , hence we retry from here */
		if ((tport->flags & FNIC_FDLS_RETRY_FRAME)
			&& (tport->retry_counter < FDLS_RETRY_COUNT)) {
			tport->flags &= ~FNIC_FDLS_RETRY_FRAME;
			fdls_send_tgt_prli(iport, tport);
		} else if (!(tport->flags & FNIC_FDLS_TGT_ABORT_ISSUED)) {
			/* PRLI has time out, send abts */
			fdls_send_tport_abts(iport, tport);
		} else {
			/* ABTS has timed out for prli, we go back to PLOGI */
			fdls_schedule_oxid_free(iport, &tport->active_oxid);
			fdls_send_tgt_plogi(iport, tport);
			fdls_set_tport_state(tport, FDLS_TGT_STATE_PLOGI);
		}
		break;
	case FDLS_TGT_STATE_ADISC:
		/* ADISC timed out send an ABTS */
		if (!(tport->flags & FNIC_FDLS_TGT_ABORT_ISSUED)) {
			fdls_send_tport_abts(iport, tport);
		} else if ((tport->flags & FNIC_FDLS_TGT_ABORT_ISSUED)
				   && (tport->retry_counter < FDLS_RETRY_COUNT)) {
			/*
			 * ABTS has timed out
			 */
			fdls_schedule_oxid_free(iport, &tport->active_oxid);
			fdls_send_tgt_adisc(iport, tport);
		} else {
			/* exceeded retry count */
			fdls_schedule_oxid_free(iport, &tport->active_oxid);
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "ADISC not responding. Deleting target port: 0x%x",
					 tport->fcid);
			fdls_send_delete_tport_msg(tport);
		}
		break;
	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "oxid: 0x%x Unknown tport state: 0x%x", oxid, tport->state);
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

	if ((fnic_fdmi_support == 1) && (!(iport->flags & FNIC_FDMI_ACTIVE))) {
		/* we can do FDMI at the same time */
		iport->fabric.fdmi_retry = 0;
		timer_setup(&iport->fabric.fdmi_timer, fdls_fdmi_timer_callback,
					0);
		fdls_send_fdmi_plogi(iport);
		iport->flags |= FNIC_FDMI_ACTIVE;
	}
}
static void
fdls_process_tgt_adisc_rsp(struct fnic_iport_s *iport,
			   struct fc_frame_header *fchdr)
{
	uint32_t tgt_fcid;
	struct fnic_tport_s *tport;
	uint8_t *fcid;
	uint64_t frame_wwnn;
	uint64_t frame_wwpn;
	uint16_t oxid;
	struct fc_std_els_adisc *adisc_rsp = (struct fc_std_els_adisc *)fchdr;
	struct fc_std_els_rjt_rsp *els_rjt = (struct fc_std_els_rjt_rsp *)fchdr;
	struct fnic *fnic = iport->fnic;

	fcid = FNIC_STD_GET_S_ID(fchdr);
	tgt_fcid = ntoh24(fcid);
	tport = fnic_find_tport_by_fcid(iport, tgt_fcid);

	if (!tport) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Tgt ADISC response tport not found: 0x%x", tgt_fcid);
		return;
	}
	if ((iport->state != FNIC_IPORT_STATE_READY)
		|| (tport->state != FDLS_TGT_STATE_ADISC)
		|| (tport->flags & FNIC_FDLS_TGT_ABORT_ISSUED)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Dropping this ADISC response");
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "iport state: %d tport state: %d Is abort issued on PRLI? %d",
			 iport->state, tport->state,
			 (tport->flags & FNIC_FDLS_TGT_ABORT_ISSUED));
		return;
	}
	if (FNIC_STD_GET_OX_ID(fchdr) != tport->active_oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Dropping frame from target: 0x%x",
			 tgt_fcid);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Reason: Stale ADISC/Aborted ADISC/OOO frame delivery");
		return;
	}

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	fdls_free_oxid(iport, oxid, &tport->active_oxid);

	switch (adisc_rsp->els.adisc_cmd) {
	case ELS_LS_ACC:
		atomic64_inc(&iport->iport_stats.tport_adisc_ls_accepts);
		if (tport->timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "tport 0x%p Canceling fabric disc timer\n",
						 tport);
			fnic_del_tport_timer_sync(fnic, tport);
		}
		tport->timer_pending = 0;
		tport->retry_counter = 0;
		frame_wwnn = get_unaligned_be64(&adisc_rsp->els.adisc_wwnn);
		frame_wwpn = get_unaligned_be64(&adisc_rsp->els.adisc_wwpn);
		if ((frame_wwnn == tport->wwnn) && (frame_wwpn == tport->wwpn)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "ADISC accepted from target: 0x%x. Target logged in",
				 tgt_fcid);
			fdls_set_tport_state(tport, FDLS_TGT_STATE_READY);
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "Error mismatch frame: ADISC");
		}
		break;

	case ELS_LS_RJT:
		atomic64_inc(&iport->iport_stats.tport_adisc_ls_rejects);
		if (((els_rjt->rej.er_reason == ELS_RJT_BUSY)
		     || (els_rjt->rej.er_reason == ELS_RJT_UNAB))
			&& (tport->retry_counter < FDLS_RETRY_COUNT)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "ADISC ret ELS_LS_RJT BUSY. Retry from timer routine: 0x%x",
				 tgt_fcid);

			/* Retry ADISC again from the timer routine. */
			tport->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "ADISC returned ELS_LS_RJT from target: 0x%x",
						 tgt_fcid);
			fdls_delete_tport(iport, tport);
		}
		break;
	}
}
static void
fdls_process_tgt_plogi_rsp(struct fnic_iport_s *iport,
			   struct fc_frame_header *fchdr)
{
	uint32_t tgt_fcid;
	struct fnic_tport_s *tport;
	uint8_t *fcid;
	uint16_t oxid;
	struct fc_std_flogi *plogi_rsp = (struct fc_std_flogi *)fchdr;
	struct fc_std_els_rjt_rsp *els_rjt = (struct fc_std_els_rjt_rsp *)fchdr;
	uint16_t max_payload_size;
	struct fnic *fnic = iport->fnic;

	fcid = FNIC_STD_GET_S_ID(fchdr);
	tgt_fcid = ntoh24(fcid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "FDLS processing target PLOGI response: tgt_fcid: 0x%x",
				 tgt_fcid);

	tport = fnic_find_tport_by_fcid(iport, tgt_fcid);
	if (!tport) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "tport not found: 0x%x", tgt_fcid);
		return;
	}
	if ((iport->state != FNIC_IPORT_STATE_READY)
		|| (tport->flags & FNIC_FDLS_TGT_ABORT_ISSUED)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Dropping frame! iport state: %d tport state: %d",
					 iport->state, tport->state);
		return;
	}

	if (tport->state != FDLS_TGT_STATE_PLOGI) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "PLOGI rsp recvd in wrong state. Drop the frame and restart nexus");
		fdls_target_restart_nexus(tport);
		return;
	}

	if (FNIC_STD_GET_OX_ID(fchdr) != tport->active_oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "PLOGI response from target: 0x%x. Dropping frame",
			 tgt_fcid);
		return;
	}

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	fdls_free_oxid(iport, oxid, &tport->active_oxid);

	switch (plogi_rsp->els.fl_cmd) {
	case ELS_LS_ACC:
		atomic64_inc(&iport->iport_stats.tport_plogi_ls_accepts);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "PLOGI accepted by target: 0x%x", tgt_fcid);
		break;

	case ELS_LS_RJT:
		atomic64_inc(&iport->iport_stats.tport_plogi_ls_rejects);
		if (((els_rjt->rej.er_reason == ELS_RJT_BUSY)
		     || (els_rjt->rej.er_reason == ELS_RJT_UNAB))
			&& (tport->retry_counter < iport->max_plogi_retries)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "PLOGI ret ELS_LS_RJT BUSY. Retry from timer routine: 0x%x",
				 tgt_fcid);
			/* Retry plogi again from the timer routine. */
			tport->flags |= FNIC_FDLS_RETRY_FRAME;
			return;
		}
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "PLOGI returned ELS_LS_RJT from target: 0x%x",
					 tgt_fcid);
		fdls_delete_tport(iport, tport);
		return;

	default:
		atomic64_inc(&iport->iport_stats.tport_plogi_misc_rejects);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "PLOGI not accepted from target fcid: 0x%x",
					 tgt_fcid);
		return;
	}

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Found the PLOGI target: 0x%x and state: %d",
				 (unsigned int) tgt_fcid, tport->state);

	if (tport->timer_pending) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "tport fcid 0x%x: Canceling disc timer\n",
					 tport->fcid);
		fnic_del_tport_timer_sync(fnic, tport);
	}

	tport->timer_pending = 0;
	tport->wwpn = get_unaligned_be64(&FNIC_LOGI_PORT_NAME(plogi_rsp->els));
	tport->wwnn = get_unaligned_be64(&FNIC_LOGI_NODE_NAME(plogi_rsp->els));

	/* Learn the Service Params */

	/* Max frame size - choose the lowest */
	max_payload_size = fnic_fc_plogi_rsp_rdf(iport, plogi_rsp);
	tport->max_payload_size =
		min(max_payload_size, iport->max_payload_size);

	if (tport->max_payload_size < FNIC_MIN_DATA_FIELD_SIZE) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "MFS: tport max frame size below spec bounds: %d",
			 tport->max_payload_size);
		tport->max_payload_size = FNIC_MIN_DATA_FIELD_SIZE;
	}

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "MAX frame size: %u iport max_payload_size: %d tport mfs: %d",
		 max_payload_size, iport->max_payload_size,
		 tport->max_payload_size);

	tport->max_concur_seqs = FNIC_FC_PLOGI_RSP_CONCUR_SEQ(plogi_rsp);

	tport->retry_counter = 0;
	fdls_set_tport_state(tport, FDLS_TGT_STATE_PRLI);
	fdls_send_tgt_prli(iport, tport);
}
static void
fdls_process_tgt_prli_rsp(struct fnic_iport_s *iport,
			  struct fc_frame_header *fchdr)
{
	uint32_t tgt_fcid;
	struct fnic_tport_s *tport;
	uint8_t *fcid;
	uint16_t oxid;
	struct fc_std_els_prli *prli_rsp = (struct fc_std_els_prli *)fchdr;
	struct fc_std_els_rjt_rsp *els_rjt = (struct fc_std_els_rjt_rsp *)fchdr;
	struct fnic_tport_event_s *tport_add_evt;
	struct fnic *fnic = iport->fnic;
	bool mismatched_tgt = false;

	fcid = FNIC_STD_GET_S_ID(fchdr);
	tgt_fcid = ntoh24(fcid);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "FDLS process tgt PRLI response: 0x%x", tgt_fcid);

	tport = fnic_find_tport_by_fcid(iport, tgt_fcid);
	if (!tport) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "tport not found: 0x%x", tgt_fcid);
		/* Handle or just drop? */
		return;
	}

	if ((iport->state != FNIC_IPORT_STATE_READY)
		|| (tport->flags & FNIC_FDLS_TGT_ABORT_ISSUED)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Dropping frame! iport st: %d tport st: %d tport fcid: 0x%x",
			 iport->state, tport->state, tport->fcid);
		return;
	}

	if (tport->state != FDLS_TGT_STATE_PRLI) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "PRLI rsp recvd in wrong state. Drop frame. Restarting nexus");
		fdls_target_restart_nexus(tport);
		return;
	}

	if (FNIC_STD_GET_OX_ID(fchdr) != tport->active_oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Dropping PRLI response from target: 0x%x ",
			 tgt_fcid);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Reason: Stale PRLI response/Aborted PDISC/OOO frame delivery");
		return;
	}

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	fdls_free_oxid(iport, oxid, &tport->active_oxid);

	switch (prli_rsp->els_prli.prli_cmd) {
	case ELS_LS_ACC:
		atomic64_inc(&iport->iport_stats.tport_prli_ls_accepts);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "PRLI accepted from target: 0x%x", tgt_fcid);

		if (prli_rsp->sp.spp_type != FC_FC4_TYPE_SCSI) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "mismatched target zoned with FC SCSI initiator: 0x%x",
				 tgt_fcid);
			mismatched_tgt = true;
		}
		if (mismatched_tgt) {
			fdls_tgt_logout(iport, tport);
			fdls_delete_tport(iport, tport);
			return;
		}
		break;
	case ELS_LS_RJT:
		atomic64_inc(&iport->iport_stats.tport_prli_ls_rejects);
		if (((els_rjt->rej.er_reason == ELS_RJT_BUSY)
		     || (els_rjt->rej.er_reason == ELS_RJT_UNAB))
			&& (tport->retry_counter < FDLS_RETRY_COUNT)) {

			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "PRLI ret ELS_LS_RJT BUSY. Retry from timer routine: 0x%x",
				 tgt_fcid);

			/*Retry Plogi again from the timer routine. */
			tport->flags |= FNIC_FDLS_RETRY_FRAME;
			return;
		}
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "PRLI returned ELS_LS_RJT from target: 0x%x",
					 tgt_fcid);

		fdls_tgt_logout(iport, tport);
		fdls_delete_tport(iport, tport);
		return;
	default:
		atomic64_inc(&iport->iport_stats.tport_prli_misc_rejects);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "PRLI not accepted from target: 0x%x", tgt_fcid);
		return;
	}

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Found the PRLI target: 0x%x and state: %d",
				 (unsigned int) tgt_fcid, tport->state);

	if (tport->timer_pending) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "tport fcid 0x%x: Canceling disc timer\n",
					 tport->fcid);
		fnic_del_tport_timer_sync(fnic, tport);
	}
	tport->timer_pending = 0;

	/* Learn Service Params */
	tport->fcp_csp = be32_to_cpu(prli_rsp->sp.spp_params);
	tport->retry_counter = 0;

	if (tport->fcp_csp & FCP_SPPF_RETRY)
		tport->tgt_flags |= FNIC_FC_RP_FLAGS_RETRY;

	/* Check if the device plays Target Mode Function */
	if (!(tport->fcp_csp & FCP_PRLI_FUNC_TARGET)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Remote port(0x%x): no target support. Deleting it\n",
			 tgt_fcid);
		fdls_tgt_logout(iport, tport);
		fdls_delete_tport(iport, tport);
		return;
	}

	fdls_set_tport_state(tport, FDLS_TGT_STATE_READY);

	/* Inform the driver about new target added */
	tport_add_evt = kzalloc(sizeof(struct fnic_tport_event_s), GFP_ATOMIC);
	if (!tport_add_evt) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "tport event memory allocation failure: 0x%0x\n",
				 tport->fcid);
		return;
	}
	tport_add_evt->event = TGT_EV_RPORT_ADD;
	tport_add_evt->arg1 = (void *) tport;
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "iport fcid: 0x%x add tport event fcid: 0x%x\n",
			 tport->fcid, iport->fcid);
	list_add_tail(&tport_add_evt->links, &fnic->tport_event_list);
	queue_work(fnic_event_queue, &fnic->tport_work);
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "RFF_ID resp recvd in state(%d). Dropping.",
					 fdls_get_state(fdls));
		return;
	}

	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
		return;
	}

	rsp = FNIC_STD_GET_FC_CT_CMD((&rff_rsp->fc_std_ct_hdr));
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS process RFF ID response: 0x%04x", iport->fcid,
				 (uint32_t) rsp);

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (rsp) {
	case FC_FS_ACC:
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "RFF_ID ret ELS_LS_RJT BUSY. Retry from timer routine %p",
					 iport);

			/* Retry again from the timer routine */
			fdls->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "RFF_ID returned ELS_LS_RJT. Halting discovery %p",
			 iport);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "RFT_ID resp recvd in state(%d). Dropping.",
					 fdls_get_state(fdls));
		return;
	}

	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
		return;
	}


	rsp = FNIC_STD_GET_FC_CT_CMD((&rft_rsp->fc_std_ct_hdr));
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS process RFT ID response: 0x%04x", iport->fcid,
				 (uint32_t) rsp);

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (rsp) {
	case FC_FS_ACC:
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: RFT_ID ret ELS_LS_RJT BUSY. Retry from timer routine",
				 iport->fcid);

			/* Retry again from the timer routine */
			fdls->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: RFT_ID REJ. Halting discovery reason %d expl %d",
				 iport->fcid, reason_code,
			 rft_rsp->fc_std_ct_hdr.ct_explan);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "RPN_ID resp recvd in state(%d). Dropping.",
					 fdls_get_state(fdls));
		return;
	}
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
		return;
	}

	rsp = FNIC_STD_GET_FC_CT_CMD((&rpn_rsp->fc_std_ct_hdr));
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS process RPN ID response: 0x%04x", iport->fcid,
				 (uint32_t) rsp);
	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (rsp) {
	case FC_FS_ACC:
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "RPN_ID returned REJ BUSY. Retry from timer routine %p",
					 iport);

			/* Retry again from the timer routine */
			fdls->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "RPN_ID ELS_LS_RJT. Halting discovery %p", iport);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "FDLS process SCR response: 0x%04x",
		 (uint32_t) scr_rsp->scr.scr_cmd);

	if (fdls_get_state(fdls) != FDLS_STATE_SCR) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "SCR resp recvd in state(%d). Dropping.",
					 fdls_get_state(fdls));
		return;
	}
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
	}

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (scr_rsp->scr.scr_cmd) {
	case ELS_LS_ACC:
		atomic64_inc(&iport->iport_stats.fabric_scr_ls_accepts);
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "Canceling fabric disc timer %p\n", iport);
			fnic_del_fabric_timer_sync(fnic);
		}
		iport->fabric.timer_pending = 0;
		iport->fabric.retry_counter = 0;
		fdls_send_gpn_ft(iport, FDLS_STATE_GPN_FT);
		break;

	case ELS_LS_RJT:
		atomic64_inc(&iport->iport_stats.fabric_scr_ls_rejects);
		if (((els_rjt->rej.er_reason == ELS_RJT_BUSY)
	     || (els_rjt->rej.er_reason == ELS_RJT_UNAB))
			&& (fdls->retry_counter < FDLS_RETRY_COUNT)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "SCR ELS_LS_RJT BUSY. Retry from timer routine %p",
						 iport);
			/* Retry again from the timer routine */
			fdls->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "SCR returned ELS_LS_RJT. Halting discovery %p",
						 iport);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					     "Canceling fabric disc timer %p\n",
					     iport);
				fnic_del_fabric_timer_sync(fnic);
			}
			fdls->timer_pending = 0;
			fdls->retry_counter = 0;
		}
		break;

	default:
		atomic64_inc(&iport->iport_stats.fabric_scr_misc_rejects);
		break;
	}
}

static void
fdls_process_gpn_ft_tgt_list(struct fnic_iport_s *iport,
			     struct fc_frame_header *fchdr, int len)
{
	struct fc_gpn_ft_rsp_iu *gpn_ft_tgt;
	struct fnic_tport_s *tport, *next;
	uint32_t fcid;
	uint64_t wwpn;
	int rem_len = len;
	u32 old_link_down_cnt = iport->fnic->link_down_cnt;
	struct fnic *fnic = iport->fnic;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS process GPN_FT tgt list", iport->fcid);

	gpn_ft_tgt =
	    (struct fc_gpn_ft_rsp_iu *)((uint8_t *) fchdr +
					sizeof(struct fc_frame_header)
					+ sizeof(struct fc_ct_hdr));
	len -= sizeof(struct fc_frame_header) + sizeof(struct fc_ct_hdr);

	while (rem_len > 0) {

		fcid = ntoh24(gpn_ft_tgt->fcid);
		wwpn = be64_to_cpu(gpn_ft_tgt->wwpn);

		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "tport: 0x%x: ctrl:0x%x", fcid, gpn_ft_tgt->ctrl);

		if (fcid == iport->fcid) {
			if (gpn_ft_tgt->ctrl & FC_NS_FID_LAST)
				break;
			gpn_ft_tgt++;
			rem_len -= sizeof(struct fc_gpn_ft_rsp_iu);
			continue;
		}

		tport = fnic_find_tport_by_wwpn(iport, wwpn);
		if (!tport) {
			/*
			 * New port registered with the switch or first time query
			 */
			tport = fdls_create_tport(iport, fcid, wwpn);
			if (!tport)
				return;
		}
		/*
		 * check if this was an existing tport with same fcid
		 * but whose wwpn has changed now ,then remove it and
		 * create a new one
		 */
		if (tport->fcid != fcid) {
			fdls_delete_tport(iport, tport);
			tport = fdls_create_tport(iport, fcid, wwpn);
			if (!tport)
				return;
		}

		/*
		 * If this GPN_FT rsp is after RSCN then mark the tports which
		 * matches with the new GPN_FT list, if some tport is not
		 * found in GPN_FT we went to delete that tport later.
		 */
		if (fdls_get_state((&iport->fabric)) == FDLS_STATE_RSCN_GPN_FT)
			tport->flags |= FNIC_FDLS_TPORT_IN_GPN_FT_LIST;

		if (gpn_ft_tgt->ctrl & FC_NS_FID_LAST)
			break;

		gpn_ft_tgt++;
		rem_len -= sizeof(struct fc_gpn_ft_rsp_iu);
	}
	if (rem_len <= 0) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "GPN_FT response: malformed/corrupt frame rxlen: %d remlen: %d",
			 len, rem_len);
}

	/*remove those ports which was not listed in GPN_FT */
	if (fdls_get_state((&iport->fabric)) == FDLS_STATE_RSCN_GPN_FT) {
		list_for_each_entry_safe(tport, next, &iport->tport_list, links) {

			if (!(tport->flags & FNIC_FDLS_TPORT_IN_GPN_FT_LIST)) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Remove port: 0x%x not found in GPN_FT list",
					 tport->fcid);
				fdls_delete_tport(iport, tport);
			} else {
				tport->flags &= ~FNIC_FDLS_TPORT_IN_GPN_FT_LIST;
			}
			if ((old_link_down_cnt != iport->fnic->link_down_cnt)
				|| (iport->state != FNIC_IPORT_STATE_READY)) {
				return;
			}
		}
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
	int count = 0;
	struct fnic_tport_s *tport, *next;
	u32 old_link_down_cnt = iport->fnic->link_down_cnt;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "GPNFT resp recvd in fab state(%d) iport_state(%d). Dropping.",
			 fdls_get_state(fdls), iport->state);
		return;
	}

	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
	}

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	iport->state = FNIC_IPORT_STATE_READY;
	rsp = FNIC_STD_GET_FC_CT_CMD((&gpn_ft_rsp->fc_std_ct_hdr));

	switch (rsp) {

	case FC_FS_ACC:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "0x%x: GPNFT_RSP accept", iport->fcid);
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "0x%x: Canceling fabric disc timer\n",
						 iport->fcid);
			fnic_del_fabric_timer_sync(fnic);
		}
		iport->fabric.timer_pending = 0;
		iport->fabric.retry_counter = 0;
		fdls_process_gpn_ft_tgt_list(iport, fchdr, len);

		/*
		 * iport state can change only if link down event happened
		 * We don't need to undo fdls_process_gpn_ft_tgt_list,
		 * that will be taken care in next link up event
		 */
		if (iport->state != FNIC_IPORT_STATE_READY) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Halting target discovery: fab st: %d iport st: %d ",
				 fdls_get_state(fdls), iport->state);
			break;
		}
		fdls_tgt_discovery_start(iport);
		break;

	case FC_FS_RJT:
		reason_code = gpn_ft_rsp->fc_std_ct_hdr.ct_reason;
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "0x%x: GPNFT_RSP Reject reason: %d", iport->fcid, reason_code);

		if (((reason_code == FC_FS_RJT_BSY)
		     || (reason_code == FC_FS_RJT_UNABL))
			&& (fdls->retry_counter < FDLS_RETRY_COUNT)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: GPNFT_RSP ret REJ/BSY. Retry from timer routine",
				 iport->fcid);
			/* Retry again from the timer routine */
			fdls->flags |= FNIC_FDLS_RETRY_FRAME;
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "0x%x: GPNFT_RSP reject", iport->fcid);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "0x%x: Canceling fabric disc timer\n",
							 iport->fcid);
				fnic_del_fabric_timer_sync(fnic);
			}
			iport->fabric.timer_pending = 0;
			iport->fabric.retry_counter = 0;
			/*
			 * If GPN_FT ls_rjt then we should delete
			 * all existing tports
			 */
			count = 0;
			list_for_each_entry_safe(tport, next, &iport->tport_list,
									 links) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "GPN_FT_REJECT: Remove port: 0x%x",
							 tport->fcid);
				fdls_delete_tport(iport, tport);
				if ((old_link_down_cnt != iport->fnic->link_down_cnt)
					|| (iport->state != FNIC_IPORT_STATE_READY)) {
					return;
				}
				count++;
			}
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "GPN_FT_REJECT: Removed (0x%x) ports", count);
		}
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
	}
	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (flogo_rsp->els.fl_cmd) {
	case ELS_LS_ACC:
		if (iport->fabric.state != FDLS_STATE_FABRIC_LOGO) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Flogo response. Fabric not in LOGO state. Dropping! %p",
				 iport);
			return;
		}

		iport->fabric.state = FDLS_STATE_FLOGO_DONE;
		iport->state = FNIC_IPORT_STATE_LINK_WAIT;

		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "iport 0x%p Canceling fabric disc timer\n",
						 iport);
			fnic_del_fabric_timer_sync(fnic);
		}
		iport->fabric.timer_pending = 0;
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Flogo response from Fabric for did: 0x%x",
		     ntoh24(fchdr->fh_d_id));
		return;

	case ELS_LS_RJT:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Flogo response from Fabric for did: 0x%x returned ELS_LS_RJT",
		     ntoh24(fchdr->fh_d_id));
		return;

	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS processing FLOGI response", iport->fcid);

	if (fdls_get_state(fabric) != FDLS_STATE_FABRIC_FLOGI) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "FLOGI response received in state (%d). Dropping frame",
					 fdls_get_state(fabric));
		return;
	}
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fabric), oxid, iport->active_oxid_fabric_req);
		return;
	}

	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (flogi_rsp->els.fl_cmd) {
	case ELS_LS_ACC:
		atomic64_inc(&iport->iport_stats.fabric_flogi_ls_accepts);
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "iport fcid: 0x%x Canceling fabric disc timer\n",
						 iport->fcid);
			fnic_del_fabric_timer_sync(fnic);
		}

		iport->fabric.timer_pending = 0;
		iport->fabric.retry_counter = 0;
		fcid = FNIC_STD_GET_D_ID(fchdr);
		iport->fcid = ntoh24(fcid);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "0x%x: FLOGI response accepted", iport->fcid);

		/* Learn the Service Params */
		rdf_size = be16_to_cpu(FNIC_LOGI_RDF_SIZE(flogi_rsp->els));
		if ((rdf_size >= FNIC_MIN_DATA_FIELD_SIZE)
			&& (rdf_size < FNIC_FC_MAX_PAYLOAD_LEN))
			iport->max_payload_size = min(rdf_size,
								  iport->max_payload_size);

		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "max_payload_size from fabric: %u set: %d", rdf_size,
					 iport->max_payload_size);

		iport->r_a_tov = be32_to_cpu(FNIC_LOGI_R_A_TOV(flogi_rsp->els));
		iport->e_d_tov = be32_to_cpu(FNIC_LOGI_E_D_TOV(flogi_rsp->els));

		if (FNIC_LOGI_FEATURES(flogi_rsp->els) & FNIC_FC_EDTOV_NSEC)
			iport->e_d_tov = iport->e_d_tov / FNIC_NSEC_TO_MSEC;

		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "From fabric: R_A_TOV: %d E_D_TOV: %d",
					 iport->r_a_tov, iport->e_d_tov);

		fc_host_fabric_name(iport->fnic->host) =
		get_unaligned_be64(&FNIC_LOGI_NODE_NAME(flogi_rsp->els));
		fc_host_port_id(iport->fnic->host) = iport->fcid;

		fnic_fdls_learn_fcoe_macs(iport, rx_frame, fcid);

		if (fnic_fdls_register_portid(iport, iport->fcid, rx_frame) != 0) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "0x%x: FLOGI registration failed", iport->fcid);
			break;
		}

		memcpy(&fcmac[3], fcid, 3);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Adding vNIC device MAC addr: %02x:%02x:%02x:%02x:%02x:%02x",
			 fcmac[0], fcmac[1], fcmac[2], fcmac[3], fcmac[4],
			 fcmac[5]);
		vnic_dev_add_addr(iport->fnic->vdev, fcmac);

		if (fdls_get_state(fabric) == FDLS_STATE_FABRIC_FLOGI) {
			fnic_fdls_start_plogi(iport);
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "FLOGI response received. Starting PLOGI");
		} else {
			/* From FDLS_STATE_FABRIC_FLOGI state fabric can only go to
			 * FDLS_STATE_LINKDOWN
			 * state, hence we don't have to worry about undoing:
			 * the fnic_fdls_register_portid and vnic_dev_add_addr
			 */
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "FLOGI response received in state (%d). Dropping frame",
				 fdls_get_state(fabric));
		}
		break;

	case ELS_LS_RJT:
		atomic64_inc(&iport->iport_stats.fabric_flogi_ls_rejects);
		if (fabric->retry_counter < iport->max_flogi_retries) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "FLOGI returned ELS_LS_RJT BUSY. Retry from timer routine %p",
				 iport);

			/* Retry Flogi again from the timer routine. */
			fabric->flags |= FNIC_FDLS_RETRY_FRAME;

		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "FLOGI returned ELS_LS_RJT. Halting discovery %p",
			 iport);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "iport 0x%p Canceling fabric disc timer\n",
							 iport);
				fnic_del_fabric_timer_sync(fnic);
			}
			fabric->timer_pending = 0;
			fabric->retry_counter = 0;
		}
		break;

	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "FLOGI response not accepted: 0x%x",
		     flogi_rsp->els.fl_cmd);
		atomic64_inc(&iport->iport_stats.fabric_flogi_misc_rejects);
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Fabric PLOGI response received in state (%d). Dropping frame",
			 fdls_get_state(&iport->fabric));
		return;
	}
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fabric_req);
		return;
	}
	fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);

	switch (plogi_rsp->els.fl_cmd) {
	case ELS_LS_ACC:
		atomic64_inc(&iport->iport_stats.fabric_plogi_ls_accepts);
		if (iport->fabric.timer_pending) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
		atomic64_inc(&iport->iport_stats.fabric_plogi_ls_rejects);
		if (((els_rjt->rej.er_reason == ELS_RJT_BUSY)
	     || (els_rjt->rej.er_reason == ELS_RJT_UNAB))
			&& (iport->fabric.retry_counter < iport->max_plogi_retries)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: Fabric PLOGI ELS_LS_RJT BUSY. Retry from timer routine",
				 iport->fcid);
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: Fabric PLOGI ELS_LS_RJT. Halting discovery",
				 iport->fcid);
			if (iport->fabric.timer_pending) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "PLOGI response not accepted: 0x%x",
		     plogi_rsp->els.fl_cmd);
		atomic64_inc(&iport->iport_stats.fabric_plogi_misc_rejects);
		break;
	}
}

static void fdls_process_fdmi_plogi_rsp(struct fnic_iport_s *iport,
					struct fc_frame_header *fchdr)
{
	struct fc_std_flogi *plogi_rsp = (struct fc_std_flogi *)fchdr;
	struct fc_std_els_rjt_rsp *els_rjt = (struct fc_std_els_rjt_rsp *)fchdr;
	struct fnic_fdls_fabric_s *fdls = &iport->fabric;
	struct fnic *fnic = iport->fnic;
	u64 fdmi_tov;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);

	if (iport->active_oxid_fdmi_plogi != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. state: %d, oxid recvd: 0x%x, active oxid: 0x%x\n",
			fdls_get_state(fdls), oxid, iport->active_oxid_fdmi_plogi);
		return;
	}

	iport->fabric.fdmi_pending &= ~FDLS_FDMI_PLOGI_PENDING;
	fdls_free_oxid(iport, oxid, &iport->active_oxid_fdmi_plogi);

	if (ntoh24(fchdr->fh_s_id) == FC_FID_MGMT_SERV) {
		del_timer_sync(&iport->fabric.fdmi_timer);
		iport->fabric.fdmi_pending = 0;
		switch (plogi_rsp->els.fl_cmd) {
		case ELS_LS_ACC:
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "FDLS process fdmi PLOGI response status: ELS_LS_ACC\n");
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Sending fdmi registration for port 0x%x\n",
				 iport->fcid);

			fdls_fdmi_register_hba(iport);
			fdls_fdmi_register_pa(iport);
			fdmi_tov = jiffies + msecs_to_jiffies(5000);
			mod_timer(&iport->fabric.fdmi_timer,
				  round_jiffies(fdmi_tov));
			break;
		case ELS_LS_RJT:
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Fabric FDMI PLOGI returned ELS_LS_RJT reason: 0x%x",
				     els_rjt->rej.er_reason);

			if (((els_rjt->rej.er_reason == ELS_RJT_BUSY)
			     || (els_rjt->rej.er_reason == ELS_RJT_UNAB))
				&& (iport->fabric.fdmi_retry < 7)) {
				iport->fabric.fdmi_retry++;
				fdls_send_fdmi_plogi(iport);
			}
			break;
		default:
			break;
		}
	}
}
static void fdls_process_fdmi_reg_ack(struct fnic_iport_s *iport,
				      struct fc_frame_header *fchdr,
				      int rsp_type)
{
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;

	if (!iport->fabric.fdmi_pending) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			     "Received FDMI ack while not waiting: 0x%x\n",
			     FNIC_STD_GET_OX_ID(fchdr));
		return;
	}

	oxid =  FNIC_STD_GET_OX_ID(fchdr);

	if ((iport->active_oxid_fdmi_rhba != oxid) &&
		(iport->active_oxid_fdmi_rpa != oxid))  {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Incorrect OXID in response. oxid recvd: 0x%x, active oxids(rhba,rpa): 0x%x, 0x%x\n",
			oxid, iport->active_oxid_fdmi_rhba, iport->active_oxid_fdmi_rpa);
		return;
	}
	if (FNIC_FRAME_TYPE(oxid) == FNIC_FRAME_TYPE_FDMI_RHBA) {
		iport->fabric.fdmi_pending &= ~FDLS_FDMI_REG_HBA_PENDING;
		fdls_free_oxid(iport, oxid, &iport->active_oxid_fdmi_rhba);
	} else {
		iport->fabric.fdmi_pending &= ~FDLS_FDMI_RPA_PENDING;
		fdls_free_oxid(iport, oxid, &iport->active_oxid_fdmi_rpa);
	}

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		"iport fcid: 0x%x: Received FDMI registration ack\n",
		 iport->fcid);

	if (!iport->fabric.fdmi_pending) {
		del_timer_sync(&iport->fabric.fdmi_timer);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "iport fcid: 0x%x: Canceling FDMI timer\n",
					 iport->fcid);
	}
}

static void fdls_process_fdmi_abts_rsp(struct fnic_iport_s *iport,
				       struct fc_frame_header *fchdr)
{
	uint32_t s_id;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;

	s_id = ntoh24(FNIC_STD_GET_S_ID(fchdr));

	if (!(s_id != FC_FID_MGMT_SERV)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "Received abts rsp with invalid SID: 0x%x. Dropping frame",
			     s_id);
		return;
	}

	oxid =  FNIC_STD_GET_OX_ID(fchdr);

	switch (FNIC_FRAME_TYPE(oxid)) {
	case FNIC_FRAME_TYPE_FDMI_PLOGI:
		fdls_free_oxid(iport, oxid, &iport->active_oxid_fdmi_plogi);
		break;
	case FNIC_FRAME_TYPE_FDMI_RHBA:
		fdls_free_oxid(iport, oxid, &iport->active_oxid_fdmi_rhba);
		break;
	case FNIC_FRAME_TYPE_FDMI_RPA:
		fdls_free_oxid(iport, oxid, &iport->active_oxid_fdmi_rpa);
		break;
	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Received abts rsp with invalid oxid: 0x%x. Dropping frame",
			oxid);
		break;
	}

	del_timer_sync(&iport->fabric.fdmi_timer);
	iport->fabric.fdmi_pending &= ~FDLS_FDMI_ABORT_PENDING;

	fdls_send_fdmi_plogi(iport);
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Received abts rsp with invalid SID: 0x%x. Dropping frame",
			 s_id);
		return;
	}

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	if (iport->active_oxid_fabric_req != oxid) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Received abts rsp with invalid oxid: 0x%x. Dropping frame",
			oxid);
		return;
	}

	if (iport->fabric.timer_pending) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Canceling fabric disc timer %p\n", iport);
		fnic_del_fabric_timer_sync(fnic);
	}
	iport->fabric.timer_pending = 0;
	iport->fabric.flags &= ~FNIC_FDLS_FABRIC_ABORT_ISSUED;

	if (fchdr->fh_r_ctl == FC_RCTL_BA_ACC) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Received abts rsp BA_ACC for fabric_state: %d OX_ID: 0x%x",
		     fabric_state, be16_to_cpu(ba_acc->acc.ba_ox_id));
	} else if (fchdr->fh_r_ctl == FC_RCTL_BA_RJT) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"SCR exhausted retries. Start fabric PLOGI %p",
				 iport);
			fnic_fdls_start_plogi(iport);	/* go back to fabric Plogi */
		}
		break;
	case FNIC_FRAME_TYPE_FABRIC_RFT:
		if (iport->fabric.retry_counter < FDLS_RETRY_COUNT)
			fdls_send_register_fc4_types(iport);
		else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"RFT exhausted retries. Start fabric PLOGI %p",
				 iport);
			fnic_fdls_start_plogi(iport);	/* go back to fabric Plogi */
		}
		break;
	case FNIC_FRAME_TYPE_FABRIC_RFF:
		if (iport->fabric.retry_counter < FDLS_RETRY_COUNT)
			fdls_send_register_fc4_features(iport);
		else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"RFF exhausted retries. Start fabric PLOGI %p",
				 iport);
			fnic_fdls_start_plogi(iport);	/* go back to fabric Plogi */
		}
		break;
	case FNIC_FRAME_TYPE_FABRIC_GPN_FT:
		if (iport->fabric.retry_counter <= FDLS_RETRY_COUNT)
			fdls_send_gpn_ft(iport, fabric_state);
		else
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"GPN FT exhausted retries. Start fabric PLOGI %p",
				iport);
		break;
	default:
		/*
		 * We should not be here since we already validated rx oxid with
		 * our active_oxid_fabric_req
		 */
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Invalid OXID/active oxid 0x%x\n", oxid);
		WARN_ON(true);
		return;
	}
}

static void
fdls_process_abts_req(struct fnic_iport_s *iport, struct fc_frame_header *fchdr)
{
	uint8_t *frame;
	struct fc_std_abts_ba_acc *pba_acc;
	uint32_t nport_id;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);
	struct fnic_tport_s *tport;
	struct fnic *fnic = iport->fnic;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_abts_ba_acc);

	nport_id = ntoh24(fchdr->fh_s_id);
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Received abort from SID 0x%8x", nport_id);

	tport = fnic_find_tport_by_fcid(iport, nport_id);
	if (tport) {
		if (tport->active_oxid == oxid) {
			tport->flags |= FNIC_FDLS_TGT_ABORT_ISSUED;
			fdls_free_oxid(iport, oxid, &tport->active_oxid);
		}
	}

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"0x%x: Failed to allocate frame to send response for ABTS req",
				iport->fcid);
		return;
	}

	pba_acc = (struct fc_std_abts_ba_acc *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*pba_acc = (struct fc_std_abts_ba_acc) {
		.fchdr = {.fh_r_ctl = FC_RCTL_BA_ACC,
				.fh_f_ctl = {FNIC_FCP_RSP_FCTL, 0, 0}},
		.acc = {.ba_low_seq_cnt = 0, .ba_high_seq_cnt = cpu_to_be16(0xFFFF)}
	};

	FNIC_STD_SET_S_ID(pba_acc->fchdr, fchdr->fh_d_id);
	FNIC_STD_SET_D_ID(pba_acc->fchdr, fchdr->fh_s_id);
	FNIC_STD_SET_OX_ID(pba_acc->fchdr, FNIC_STD_GET_OX_ID(fchdr));
	FNIC_STD_SET_RX_ID(pba_acc->fchdr, FNIC_STD_GET_RX_ID(fchdr));

	pba_acc->acc.ba_rx_id = cpu_to_be16(FNIC_STD_GET_RX_ID(fchdr));
	pba_acc->acc.ba_ox_id = cpu_to_be16(FNIC_STD_GET_OX_ID(fchdr));

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "0x%x: FDLS send BA ACC with oxid: 0x%x",
		 iport->fcid, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
}

static void
fdls_process_unsupported_els_req(struct fnic_iport_s *iport,
				 struct fc_frame_header *fchdr)
{
	uint8_t *frame;
	struct fc_std_els_rjt_rsp *pls_rsp;
	uint16_t oxid;
	uint32_t d_id = ntoh24(fchdr->fh_d_id);
	struct fnic *fnic = iport->fnic;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_els_rjt_rsp);

	if (iport->fcid != d_id) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Dropping unsupported ELS with illegal frame bits 0x%x\n",
			 d_id);
		atomic64_inc(&iport->iport_stats.unsupported_frames_dropped);
		return;
	}

	if ((iport->state != FNIC_IPORT_STATE_READY)
		&& (iport->state != FNIC_IPORT_STATE_FABRIC_DISC)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Dropping unsupported ELS request in iport state: %d",
			 iport->state);
		atomic64_inc(&iport->iport_stats.unsupported_frames_dropped);
		return;
	}

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"Failed to allocate frame to send response to unsupported ELS request");
		return;
	}

	pls_rsp = (struct fc_std_els_rjt_rsp *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_els_rjt_frame(frame, iport);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: Process unsupported ELS request from SID: 0x%x",
		     iport->fcid, ntoh24(fchdr->fh_s_id));

	/* We don't support this ELS request, send a reject */
	pls_rsp->rej.er_reason = 0x0B;
	pls_rsp->rej.er_explan = 0x0;
	pls_rsp->rej.er_vendor = 0x0;

	FNIC_STD_SET_S_ID(pls_rsp->fchdr, fchdr->fh_d_id);
	FNIC_STD_SET_D_ID(pls_rsp->fchdr, fchdr->fh_s_id);
	oxid = FNIC_STD_GET_OX_ID(fchdr);
	FNIC_STD_SET_OX_ID(pls_rsp->fchdr, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
}

static void
fdls_process_rls_req(struct fnic_iport_s *iport, struct fc_frame_header *fchdr)
{
	uint8_t *frame;
	struct fc_std_rls_acc *prls_acc_rsp;
	uint16_t oxid;
	struct fnic *fnic = iport->fnic;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_rls_acc);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Process RLS request %d", iport->fnic->fnic_num);

	if ((iport->state != FNIC_IPORT_STATE_READY)
		&& (iport->state != FNIC_IPORT_STATE_FABRIC_DISC)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Received RLS req in iport state: %d. Dropping the frame.",
			 iport->state);
		return;
	}

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send RLS accept");
		return;
	}
	prls_acc_rsp = (struct fc_std_rls_acc *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);

	FNIC_STD_SET_S_ID(prls_acc_rsp->fchdr, fchdr->fh_d_id);
	FNIC_STD_SET_D_ID(prls_acc_rsp->fchdr, fchdr->fh_s_id);

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	FNIC_STD_SET_OX_ID(prls_acc_rsp->fchdr, oxid);
	FNIC_STD_SET_RX_ID(prls_acc_rsp->fchdr, FNIC_UNASSIGNED_RXID);

	FNIC_STD_SET_F_CTL(prls_acc_rsp->fchdr, FNIC_ELS_REP_FCTL << 16);
	FNIC_STD_SET_R_CTL(prls_acc_rsp->fchdr, FC_RCTL_ELS_REP);
	FNIC_STD_SET_TYPE(prls_acc_rsp->fchdr, FC_TYPE_ELS);

	prls_acc_rsp->els.rls_cmd = ELS_LS_ACC;
	prls_acc_rsp->els.rls_lesb.lesb_link_fail =
	    cpu_to_be32(iport->fnic->link_down_cnt);

	fnic_send_fcoe_frame(iport, frame, frame_size);
}

static void
fdls_process_els_req(struct fnic_iport_s *iport, struct fc_frame_header *fchdr,
					 uint32_t len)
{
	uint8_t *frame;
	struct fc_std_els_acc_rsp *pels_acc;
	uint16_t oxid;
	uint8_t *fc_payload;
	uint8_t type;
	struct fnic *fnic = iport->fnic;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET;

	fc_payload = (uint8_t *) fchdr + sizeof(struct fc_frame_header);
	type = *fc_payload;

	if ((iport->state != FNIC_IPORT_STATE_READY)
		&& (iport->state != FNIC_IPORT_STATE_FABRIC_DISC)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Dropping ELS frame type: 0x%x in iport state: %d",
				 type, iport->state);
		return;
	}
	switch (type) {
	case ELS_ECHO:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "sending LS_ACC for ECHO request %d\n",
					 iport->fnic->fnic_num);
		break;

	case ELS_RRQ:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "sending LS_ACC for RRQ request %d\n",
					 iport->fnic->fnic_num);
		break;

	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "sending LS_ACC for 0x%x ELS frame\n", type);
		break;
	}

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send ELS response for 0x%x",
				type);
		return;
	}

	if (type == ELS_ECHO) {
		/* Brocade sends a longer payload, copy all frame back */
		memcpy(frame, fchdr, len);
	}

	pels_acc = (struct fc_std_els_acc_rsp *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_els_acc_frame(frame, iport);

	FNIC_STD_SET_D_ID(pels_acc->fchdr, fchdr->fh_s_id);

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	FNIC_STD_SET_OX_ID(pels_acc->fchdr, oxid);

	if (type == ELS_ECHO)
		frame_size += len;
	else
		frame_size += sizeof(struct fc_std_els_acc_rsp);

	fnic_send_fcoe_frame(iport, frame, frame_size);
}

static void
fdls_process_tgt_abts_rsp(struct fnic_iport_s *iport,
			  struct fc_frame_header *fchdr)
{
	uint32_t s_id;
	struct fnic_tport_s *tport;
	uint32_t tport_state;
	struct fc_std_abts_ba_acc *ba_acc;
	struct fc_std_abts_ba_rjt *ba_rjt;
	uint16_t oxid;
	struct fnic *fnic = iport->fnic;
	int frame_type;

	s_id = ntoh24(fchdr->fh_s_id);
	ba_acc = (struct fc_std_abts_ba_acc *)fchdr;
	ba_rjt = (struct fc_std_abts_ba_rjt *)fchdr;

	tport = fnic_find_tport_by_fcid(iport, s_id);
	if (!tport) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					 "Received tgt abts rsp with invalid SID: 0x%x", s_id);
		return;
	}
	if (tport->timer_pending) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					 "tport 0x%p Canceling fabric disc timer\n", tport);
		fnic_del_tport_timer_sync(fnic, tport);
	}
	if (iport->state != FNIC_IPORT_STATE_READY) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					 "Received tgt abts rsp in iport state(%d). Dropping.",
					 iport->state);
		return;
	}
	tport->timer_pending = 0;
	tport->flags &= ~FNIC_FDLS_TGT_ABORT_ISSUED;
	tport_state = tport->state;
	oxid = FNIC_STD_GET_OX_ID(fchdr);

	/*This abort rsp is for ADISC */
	frame_type = FNIC_FRAME_TYPE(oxid);
	switch (frame_type) {
	case FNIC_FRAME_TYPE_TGT_ADISC:
		if (fchdr->fh_r_ctl == FC_RCTL_BA_ACC) {
			FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				     "OX_ID: 0x%x tgt_fcid: 0x%x rcvd tgt adisc abts resp BA_ACC",
				     be16_to_cpu(ba_acc->acc.ba_ox_id),
				     tport->fcid);
		} else if (fchdr->fh_r_ctl == FC_RCTL_BA_RJT) {
			FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				 "ADISC BA_RJT rcvd tport_fcid: 0x%x tport_state: %d ",
				 tport->fcid, tport_state);
			FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				 "reason code: 0x%x reason code explanation:0x%x ",
				     ba_rjt->rjt.br_reason,
				     ba_rjt->rjt.br_explan);
		}
		if ((tport->retry_counter < FDLS_RETRY_COUNT)
		    && (fchdr->fh_r_ctl == FC_RCTL_BA_ACC)) {
			fdls_free_oxid(iport, oxid, &tport->active_oxid);
			fdls_send_tgt_adisc(iport, tport);
			return;
		}
		fdls_free_oxid(iport, oxid, &tport->active_oxid);
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					 "ADISC not responding. Deleting target port: 0x%x",
					 tport->fcid);
		fdls_delete_tport(iport, tport);
		/* Restart discovery of targets */
		if ((iport->state == FNIC_IPORT_STATE_READY)
			&& (iport->fabric.state != FDLS_STATE_SEND_GPNFT)
			&& (iport->fabric.state != FDLS_STATE_RSCN_GPN_FT)) {
			fdls_send_gpn_ft(iport, FDLS_STATE_SEND_GPNFT);
		}
		break;
	case FNIC_FRAME_TYPE_TGT_PLOGI:
		if (fchdr->fh_r_ctl == FC_RCTL_BA_ACC) {
			FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				 "Received tgt PLOGI abts response BA_ACC tgt_fcid: 0x%x",
				 tport->fcid);
		} else if (fchdr->fh_r_ctl == FC_RCTL_BA_RJT) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "PLOGI BA_RJT received for tport_fcid: 0x%x OX_ID: 0x%x",
				     tport->fcid, FNIC_STD_GET_OX_ID(fchdr));
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "reason code: 0x%x reason code explanation: 0x%x",
				     ba_rjt->rjt.br_reason,
				     ba_rjt->rjt.br_explan);
		}
		if ((tport->retry_counter < iport->max_plogi_retries)
		    && (fchdr->fh_r_ctl == FC_RCTL_BA_ACC)) {
			fdls_free_oxid(iport, oxid, &tport->active_oxid);
			fdls_send_tgt_plogi(iport, tport);
			return;
		}

		fdls_free_oxid(iport, oxid, &tport->active_oxid);
		fdls_delete_tport(iport, tport);
		/* Restart discovery of targets */
		if ((iport->state == FNIC_IPORT_STATE_READY)
			&& (iport->fabric.state != FDLS_STATE_SEND_GPNFT)
			&& (iport->fabric.state != FDLS_STATE_RSCN_GPN_FT)) {
			fdls_send_gpn_ft(iport, FDLS_STATE_SEND_GPNFT);
		}
		break;
	case FNIC_FRAME_TYPE_TGT_PRLI:
		if (fchdr->fh_r_ctl == FC_RCTL_BA_ACC) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: Received tgt PRLI abts response BA_ACC",
				 tport->fcid);
		} else if (fchdr->fh_r_ctl == FC_RCTL_BA_RJT) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "PRLI BA_RJT received for tport_fcid: 0x%x OX_ID: 0x%x ",
				     tport->fcid, FNIC_STD_GET_OX_ID(fchdr));
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "reason code: 0x%x reason code explanation: 0x%x",
				     ba_rjt->rjt.br_reason,
				     ba_rjt->rjt.br_explan);
		}
		if ((tport->retry_counter < FDLS_RETRY_COUNT)
		    && (fchdr->fh_r_ctl == FC_RCTL_BA_ACC)) {
			fdls_free_oxid(iport, oxid, &tport->active_oxid);
			fdls_send_tgt_prli(iport, tport);
			return;
		}
		fdls_free_oxid(iport, oxid, &tport->active_oxid);
		fdls_send_tgt_plogi(iport, tport);	/* go back to plogi */
		fdls_set_tport_state(tport, FDLS_TGT_STATE_PLOGI);
		break;
	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Received ABTS response for unknown frame %p", iport);
		break;
	}

}

static void
fdls_process_plogi_req(struct fnic_iport_s *iport,
		       struct fc_frame_header *fchdr)
{
	uint8_t *frame;
	struct fc_std_els_rjt_rsp *pplogi_rsp;
	uint16_t oxid;
	uint32_t d_id = ntoh24(fchdr->fh_d_id);
	struct fnic *fnic = iport->fnic;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_els_rjt_rsp);

	if (iport->fcid != d_id) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Received PLOGI with illegal frame bits. Dropping frame from 0x%x",
			 d_id);
		return;
	}

	if (iport->state != FNIC_IPORT_STATE_READY) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Received PLOGI request in iport state: %d Dropping frame",
			 iport->state);
		return;
	}

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"Failed to allocate frame to send response to PLOGI request");
		return;
	}

	pplogi_rsp = (struct fc_std_els_rjt_rsp *) (frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	fdls_init_els_rjt_frame(frame, iport);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: Process PLOGI request from SID: 0x%x",
				 iport->fcid, ntoh24(fchdr->fh_s_id));

	/* We don't support PLOGI request, send a reject */
	pplogi_rsp->rej.er_reason = 0x0B;
	pplogi_rsp->rej.er_explan = 0x0;
	pplogi_rsp->rej.er_vendor = 0x0;

	FNIC_STD_SET_S_ID(pplogi_rsp->fchdr, fchdr->fh_d_id);
	FNIC_STD_SET_D_ID(pplogi_rsp->fchdr, fchdr->fh_s_id);
	oxid = FNIC_STD_GET_OX_ID(fchdr);
	FNIC_STD_SET_OX_ID(pplogi_rsp->fchdr, oxid);

	fnic_send_fcoe_frame(iport, frame, frame_size);
}

static void
fdls_process_logo_req(struct fnic_iport_s *iport, struct fc_frame_header *fchdr)
{
	struct fc_std_logo *logo = (struct fc_std_logo *)fchdr;
	uint32_t nport_id;
	uint64_t nport_name;
	struct fnic_tport_s *tport;
	struct fnic *fnic = iport->fnic;
	uint16_t oxid;

	nport_id = ntoh24(logo->els.fl_n_port_id);
	nport_name = be64_to_cpu(logo->els.fl_n_port_wwn);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Process LOGO request from fcid: 0x%x", nport_id);

	if (iport->state != FNIC_IPORT_STATE_READY) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			 "Dropping LOGO req from 0x%x in iport state: %d",
			 nport_id, iport->state);
		return;
	}

	tport = fnic_find_tport_by_fcid(iport, nport_id);

	if (!tport) {
		/* We are not logged in with the nport, log and drop... */
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			 "Received LOGO from an nport not logged in: 0x%x(0x%llx)",
			 nport_id, nport_name);
		return;
	}
	if (tport->fcid != nport_id) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		 "Received LOGO with invalid target port fcid: 0x%x(0x%llx)",
		 nport_id, nport_name);
		return;
	}
	if (tport->timer_pending) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					 "tport fcid 0x%x: Canceling disc timer\n",
					 tport->fcid);
		fnic_del_tport_timer_sync(fnic, tport);
		tport->timer_pending = 0;
	}

	/* got a logo in response to adisc to a target which has logged out */
	if (tport->state == FDLS_TGT_STATE_ADISC) {
		tport->retry_counter = 0;
		oxid = tport->active_oxid;
		fdls_free_oxid(iport, oxid, &tport->active_oxid);
		fdls_delete_tport(iport, tport);
		fdls_send_logo_resp(iport, &logo->fchdr);
		if ((iport->state == FNIC_IPORT_STATE_READY)
			&& (fdls_get_state(&iport->fabric) != FDLS_STATE_SEND_GPNFT)
			&& (fdls_get_state(&iport->fabric) != FDLS_STATE_RSCN_GPN_FT)) {
			FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
						 "Sending GPNFT in response to LOGO from Target:0x%x",
						 nport_id);
			fdls_send_gpn_ft(iport, FDLS_STATE_SEND_GPNFT);
			return;
		}
	} else {
		fdls_delete_tport(iport, tport);
	}
	if (iport->state == FNIC_IPORT_STATE_READY) {
		fdls_send_logo_resp(iport, &logo->fchdr);
		if ((fdls_get_state(&iport->fabric) != FDLS_STATE_SEND_GPNFT) &&
			(fdls_get_state(&iport->fabric) != FDLS_STATE_RSCN_GPN_FT)) {
			FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
						 "Sending GPNFT in response to LOGO from Target:0x%x",
						 nport_id);
			fdls_send_gpn_ft(iport, FDLS_STATE_SEND_GPNFT);
		}
	}
}

static void
fdls_process_rscn(struct fnic_iport_s *iport, struct fc_frame_header *fchdr)
{
	struct fc_std_rscn *rscn;
	struct fc_els_rscn_page *rscn_port = NULL;
	int num_ports;
	struct fnic_tport_s *tport, *next;
	uint32_t nport_id;
	uint8_t fcid[3];
	int newports = 0;
	struct fnic_fdls_fabric_s *fdls = &iport->fabric;
	struct fnic *fnic = iport->fnic;
	int rscn_type = NOT_PC_RSCN;
	uint32_t sid = ntoh24(fchdr->fh_s_id);
	unsigned long reset_fnic_list_lock_flags = 0;
	uint16_t rscn_payload_len;

	atomic64_inc(&iport->iport_stats.num_rscns);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "FDLS process RSCN %p", iport);

	if (iport->state != FNIC_IPORT_STATE_READY) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "FDLS RSCN received in state(%d). Dropping",
					 fdls_get_state(fdls));
		return;
	}

	rscn = (struct fc_std_rscn *)fchdr;
	rscn_payload_len = be16_to_cpu(rscn->els.rscn_plen);

	/* frame validation */
	if ((rscn_payload_len % 4 != 0) || (rscn_payload_len < 8)
	    || (rscn_payload_len > 1024)
	    || (rscn->els.rscn_page_len != 4)) {
		num_ports = 0;
		if ((rscn_payload_len == 0xFFFF)
		    && (sid == FC_FID_FCTRL)) {
			rscn_type = PC_RSCN;
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				     "pcrscn: PCRSCN received. sid: 0x%x payload len: 0x%x",
				     sid, rscn_payload_len);
		} else {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "RSCN payload_len: 0x%x page_len: 0x%x",
				     rscn_payload_len, rscn->els.rscn_page_len);
			/* if this happens then we need to send ADISC to all the tports. */
			list_for_each_entry_safe(tport, next, &iport->tport_list, links) {
				if (tport->state == FDLS_TGT_STATE_READY)
					tport->flags |= FNIC_FDLS_TPORT_SEND_ADISC;
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "RSCN for port id: 0x%x", tport->fcid);
			}
		} /* end else */
	} else {
		num_ports = (rscn_payload_len - 4) / rscn->els.rscn_page_len;
		rscn_port = (struct fc_els_rscn_page *)(rscn + 1);
	}
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "RSCN received for num_ports: %d payload_len: %d page_len: %d ",
		     num_ports, rscn_payload_len, rscn->els.rscn_page_len);

	/*
	 * RSCN have at least one Port_ID page , but may not have any port_id
	 * in it. If no port_id is specified in the Port_ID page , we send
	 * ADISC to all the tports
	 */

	while (num_ports) {

		memcpy(fcid, rscn_port->rscn_fid, 3);

		nport_id = ntoh24(fcid);
		rscn_port++;
		num_ports--;
		/* if this happens then we need to send ADISC to all the tports. */
		if (nport_id == 0) {
			list_for_each_entry_safe(tport, next, &iport->tport_list,
									 links) {
				if (tport->state == FDLS_TGT_STATE_READY)
					tport->flags |= FNIC_FDLS_TPORT_SEND_ADISC;

				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "RSCN for port id: 0x%x", tport->fcid);
			}
			break;
		}
		tport = fnic_find_tport_by_fcid(iport, nport_id);

		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "RSCN port id list: 0x%x", nport_id);

		if (!tport) {
			newports++;
			continue;
		}
		if (tport->state == FDLS_TGT_STATE_READY)
			tport->flags |= FNIC_FDLS_TPORT_SEND_ADISC;
	}

	if (pc_rscn_handling_feature_flag == PC_RSCN_HANDLING_FEATURE_ON &&
		rscn_type == PC_RSCN && fnic->role == FNIC_ROLE_FCP_INITIATOR) {

		if (fnic->pc_rscn_handling_status == PC_RSCN_HANDLING_IN_PROGRESS) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "PCRSCN handling already in progress. Skip host reset: %d",
				 iport->fnic->fnic_num);
			return;
		}

		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Processing PCRSCN. Queuing fnic for host reset: %d",
			 iport->fnic->fnic_num);
		fnic->pc_rscn_handling_status = PC_RSCN_HANDLING_IN_PROGRESS;

		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);

		spin_lock_irqsave(&reset_fnic_list_lock,
						  reset_fnic_list_lock_flags);
		list_add_tail(&fnic->links, &reset_fnic_list);
		spin_unlock_irqrestore(&reset_fnic_list_lock,
							   reset_fnic_list_lock_flags);

		queue_work(reset_fnic_work_queue, &reset_fnic_work);
		spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	} else {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "FDLS process RSCN sending GPN_FT: newports: %d", newports);
		fdls_send_gpn_ft(iport, FDLS_STATE_RSCN_GPN_FT);
		fdls_send_rscn_resp(iport, fchdr);
	}
}

void fnic_fdls_disc_start(struct fnic_iport_s *iport)
{
	struct fnic *fnic = iport->fnic;

	fc_host_fabric_name(iport->fnic->host) = 0;
	fc_host_post_event(iport->fnic->host, fc_get_event_number(),
					   FCH_EVT_LIPRESET, 0);

	if (!iport->usefip) {
		if (iport->flags & FNIC_FIRST_LINK_UP) {
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			fnic_scsi_fcpio_reset(iport->fnic);
			spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

			iport->flags &= ~FNIC_FIRST_LINK_UP;
		}
		fnic_fdls_start_flogi(iport);
	} else
		fnic_fdls_start_plogi(iport);
}

static void
fdls_process_adisc_req(struct fnic_iport_s *iport,
		       struct fc_frame_header *fchdr)
{
	struct fc_std_els_adisc *padisc_acc;
	struct fc_std_els_adisc *adisc_req = (struct fc_std_els_adisc *)fchdr;
	uint64_t frame_wwnn;
	uint64_t frame_wwpn;
	uint32_t tgt_fcid;
	struct fnic_tport_s *tport;
	uint8_t *fcid;
	uint8_t *rjt_frame;
	uint8_t *acc_frame;
	struct fc_std_els_rjt_rsp *prjts_rsp;
	uint16_t oxid;
	struct fnic *fnic = iport->fnic;
	uint16_t rjt_frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_els_rjt_rsp);
	uint16_t acc_frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_std_els_adisc);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Process ADISC request %d", iport->fnic->fnic_num);

	fcid = FNIC_STD_GET_S_ID(fchdr);
	tgt_fcid = ntoh24(fcid);
	tport = fnic_find_tport_by_fcid(iport, tgt_fcid);
	if (!tport) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					 "tport for fcid: 0x%x not found. Dropping ADISC req.",
					 tgt_fcid);
		return;
	}
	if (iport->state != FNIC_IPORT_STATE_READY) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			 "Dropping ADISC req from fcid: 0x%x in iport state: %d",
			 tgt_fcid, iport->state);
		return;
	}

	frame_wwnn = be64_to_cpu(adisc_req->els.adisc_wwnn);
	frame_wwpn = be64_to_cpu(adisc_req->els.adisc_wwpn);

	if ((frame_wwnn != tport->wwnn) || (frame_wwpn != tport->wwpn)) {
		/* send reject */
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			 "ADISC req from fcid: 0x%x mismatch wwpn: 0x%llx wwnn: 0x%llx",
			 tgt_fcid, frame_wwpn, frame_wwnn);
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			 "local tport wwpn: 0x%llx wwnn: 0x%llx. Sending RJT",
			 tport->wwpn, tport->wwnn);

		rjt_frame = fdls_alloc_frame(iport);
		if (rjt_frame == NULL) {
			FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate rjt_frame to send response to ADISC request");
			return;
		}

		prjts_rsp = (struct fc_std_els_rjt_rsp *) (rjt_frame + FNIC_ETH_FCOE_HDRS_OFFSET);
		fdls_init_els_rjt_frame(rjt_frame, iport);

		prjts_rsp->rej.er_reason = 0x03;	/*  logical error */
		prjts_rsp->rej.er_explan = 0x1E;	/*  N_port login required */
		prjts_rsp->rej.er_vendor = 0x0;

		FNIC_STD_SET_S_ID(prjts_rsp->fchdr, fchdr->fh_d_id);
		FNIC_STD_SET_D_ID(prjts_rsp->fchdr, fchdr->fh_s_id);
		oxid = FNIC_STD_GET_OX_ID(fchdr);
		FNIC_STD_SET_OX_ID(prjts_rsp->fchdr, oxid);

		fnic_send_fcoe_frame(iport, rjt_frame, rjt_frame_size);
		return;
	}

	acc_frame = fdls_alloc_frame(iport);
	if (acc_frame == NULL) {
		FNIC_FCS_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to allocate frame to send ADISC accept");
		return;
	}

	padisc_acc = (struct fc_std_els_adisc *) (acc_frame + FNIC_ETH_FCOE_HDRS_OFFSET);

	FNIC_STD_SET_S_ID(padisc_acc->fchdr, fchdr->fh_d_id);
	FNIC_STD_SET_D_ID(padisc_acc->fchdr, fchdr->fh_s_id);

	FNIC_STD_SET_F_CTL(padisc_acc->fchdr, FNIC_ELS_REP_FCTL << 16);
	FNIC_STD_SET_R_CTL(padisc_acc->fchdr, FC_RCTL_ELS_REP);
	FNIC_STD_SET_TYPE(padisc_acc->fchdr, FC_TYPE_ELS);

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	FNIC_STD_SET_OX_ID(padisc_acc->fchdr, oxid);
	FNIC_STD_SET_RX_ID(padisc_acc->fchdr, FNIC_UNASSIGNED_RXID);

	padisc_acc->els.adisc_cmd = ELS_LS_ACC;

	FNIC_STD_SET_NPORT_NAME(&padisc_acc->els.adisc_wwpn,
			iport->wwpn);
	FNIC_STD_SET_NODE_NAME(&padisc_acc->els.adisc_wwnn,
			iport->wwnn);
	memcpy(padisc_acc->els.adisc_port_id, fchdr->fh_d_id, 3);

	fnic_send_fcoe_frame(iport, acc_frame, acc_frame_size);
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
		if (iport->fcid != d_id || (!FNIC_FC_FRAME_CS_CTL(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				     "invalid frame received. Dropping frame");
			return -1;
		}
	}

	/*  BLS ABTS response */
	if ((fchdr->fh_r_ctl == FC_RCTL_BA_ACC)
	|| (fchdr->fh_r_ctl == FC_RCTL_BA_RJT)) {
		if (!(FNIC_FC_FRAME_TYPE_BLS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "Received ABTS invalid frame. Dropping frame");
			return -1;

		}
		if (fdls_is_oxid_fabric_req(oxid)) {
			if (!(iport->fabric.flags & FNIC_FDLS_FABRIC_ABORT_ISSUED)) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"Received ABTS rsp with unknown oxid(0x%x) from 0x%x. Dropping frame",
			oxid, s_id);
		return -1;
	}

	/* BLS ABTS Req */
	if ((fchdr->fh_r_ctl == FC_RCTL_BA_ABTS)
	&& (FNIC_FC_FRAME_TYPE_BLS(fchdr))) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "Received LOGO invalid frame. Dropping frame");
				return -1;
			}
			return FNIC_ELS_LOGO_REQ;
		case ELS_RSCN:
			if ((!FNIC_FC_FRAME_FCTL_FIRST_LAST_SEQINIT(fchdr))
				|| (!FNIC_FC_FRAME_TYPE_ELS(fchdr))
				|| (!FNIC_FC_FRAME_UNSOLICITED(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "Received RSCN invalid FCTL. Dropping frame");
				return -1;
			}
			if (s_id != FC_FID_FCTRL)
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Unsupported frame (type:0x%02x) from fcid: 0x%x",
				 type, s_id);
			return FNIC_ELS_UNSUPPORTED_REQ;
		}
	}

	/* solicited response from fabric or target */
	oxid_frame_type = FNIC_FRAME_TYPE(oxid);
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"oxid frame code: 0x%x, oxid: 0x%x\n", oxid_frame_type, oxid);
	switch (oxid_frame_type) {
	case FNIC_FRAME_TYPE_FABRIC_FLOGI:
		if (type == ELS_LS_ACC) {
			if ((s_id != FC_FID_FLOGI)
				|| (!FNIC_FC_FRAME_TYPE_ELS(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
				return -1;
			}
		}
		return FNIC_FABRIC_FLOGI_RSP;

	case FNIC_FRAME_TYPE_FABRIC_PLOGI:
		if (type == ELS_LS_ACC) {
			if ((s_id != FC_FID_DIR_SERV)
				|| (!FNIC_FC_FRAME_TYPE_ELS(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
				return -1;
			}
		}
		return FNIC_FABRIC_PLOGI_RSP;

	case FNIC_FRAME_TYPE_FABRIC_SCR:
		if (type == ELS_LS_ACC) {
			if ((s_id != FC_FID_FCTRL)
				|| (!FNIC_FC_FRAME_TYPE_ELS(fchdr))) {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
				return -1;
			}
		}
		return FNIC_FABRIC_SCR_RSP;

	case FNIC_FRAME_TYPE_FABRIC_RPN:
		if ((s_id != FC_FID_DIR_SERV) || (!FNIC_FC_FRAME_TYPE_FC_GS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
			return -1;
		}
		return FNIC_FABRIC_RPN_RSP;

	case FNIC_FRAME_TYPE_FABRIC_RFT:
		if ((s_id != FC_FID_DIR_SERV) || (!FNIC_FC_FRAME_TYPE_FC_GS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
			return -1;
		}
		return FNIC_FABRIC_RFT_RSP;

	case FNIC_FRAME_TYPE_FABRIC_RFF:
		if ((s_id != FC_FID_DIR_SERV) || (!FNIC_FC_FRAME_TYPE_FC_GS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
			return -1;
		}
		return FNIC_FABRIC_RFF_RSP;

	case FNIC_FRAME_TYPE_FABRIC_GPN_FT:
		if ((s_id != FC_FID_DIR_SERV) || (!FNIC_FC_FRAME_TYPE_FC_GS(fchdr))) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Received unknown frame. Dropping frame");
			return -1;
		}
		return FNIC_FABRIC_GPN_FT_RSP;

	case FNIC_FRAME_TYPE_FABRIC_LOGO:
		return FNIC_FABRIC_LOGO_RSP;
	case FNIC_FRAME_TYPE_FDMI_PLOGI:
		return FNIC_FDMI_PLOGI_RSP;
	case FNIC_FRAME_TYPE_FDMI_RHBA:
		return FNIC_FDMI_REG_HBA_RSP;
	case FNIC_FRAME_TYPE_FDMI_RPA:
		return FNIC_FDMI_RPA_RSP;
	case FNIC_FRAME_TYPE_TGT_PLOGI:
		return FNIC_TPORT_PLOGI_RSP;
	case FNIC_FRAME_TYPE_TGT_PRLI:
		return FNIC_TPORT_PRLI_RSP;
	case FNIC_FRAME_TYPE_TGT_ADISC:
		return FNIC_TPORT_ADISC_RSP;
	case FNIC_FRAME_TYPE_TGT_LOGO:
		if (!FNIC_FC_FRAME_TYPE_ELS(fchdr)) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"Dropping Unknown frame in tport solicited exchange range type: 0x%x.",
				     fchdr->fh_type);
			return -1;
		}
		return FNIC_TPORT_LOGO_RSP;
	default:
		/* Drop the Rx frame and log/stats it */
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
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
	case FNIC_FDMI_PLOGI_RSP:
		fdls_process_fdmi_plogi_rsp(iport, fchdr);
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
	case FNIC_TPORT_PLOGI_RSP:
		fdls_process_tgt_plogi_rsp(iport, fchdr);
		break;
	case FNIC_TPORT_PRLI_RSP:
		fdls_process_tgt_prli_rsp(iport, fchdr);
		break;
	case FNIC_TPORT_ADISC_RSP:
		fdls_process_tgt_adisc_rsp(iport, fchdr);
		break;
	case FNIC_TPORT_BLS_ABTS_RSP:
		fdls_process_tgt_abts_rsp(iport, fchdr);
		break;
	case FNIC_TPORT_LOGO_RSP:
		/* Logo response from tgt which we have deleted */
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Logo response from tgt: 0x%x",
			     ntoh24(fchdr->fh_s_id));
		break;
	case FNIC_FABRIC_LOGO_RSP:
		fdls_process_fabric_logo_rsp(iport, fchdr);
		break;
	case FNIC_FABRIC_BLS_ABTS_RSP:
			fdls_process_fabric_abts_rsp(iport, fchdr);
		break;
	case FNIC_FDMI_BLS_ABTS_RSP:
		fdls_process_fdmi_abts_rsp(iport, fchdr);
		break;
	case FNIC_BLS_ABTS_REQ:
		fdls_process_abts_req(iport, fchdr);
		break;
	case FNIC_ELS_UNSUPPORTED_REQ:
		fdls_process_unsupported_els_req(iport, fchdr);
		break;
	case FNIC_ELS_PLOGI_REQ:
		fdls_process_plogi_req(iport, fchdr);
		break;
	case FNIC_ELS_RSCN_REQ:
		fdls_process_rscn(iport, fchdr);
		break;
	case FNIC_ELS_LOGO_REQ:
		fdls_process_logo_req(iport, fchdr);
		break;
	case FNIC_ELS_RRQ:
	case FNIC_ELS_ECHO_REQ:
		fdls_process_els_req(iport, fchdr, len);
		break;
	case FNIC_ELS_ADISC:
		fdls_process_adisc_req(iport, fchdr);
		break;
	case FNIC_ELS_RLS:
		fdls_process_rls_req(iport, fchdr);
		break;
	case FNIC_FDMI_REG_HBA_RSP:
	case FNIC_FDMI_RPA_RSP:
		fdls_process_fdmi_reg_ack(iport, fchdr, frame_type);
		break;
	default:
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "s_id: 0x%x d_did: 0x%x", s_id, d_id);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Received unknown FCoE frame of len: %d. Dropping frame", len);
		break;
	}
}

void fnic_fdls_disc_init(struct fnic_iport_s *iport)
{
	fdls_reset_oxid_pool(iport);
	fdls_set_state((&iport->fabric), FDLS_STATE_INIT);
}

void fnic_fdls_link_down(struct fnic_iport_s *iport)
{
	struct fnic_tport_s *tport, *next;
	struct fnic *fnic = iport->fnic;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS processing link down", iport->fcid);

	fdls_set_state((&iport->fabric), FDLS_STATE_LINKDOWN);
	iport->fabric.flags = 0;

	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	fnic_scsi_fcpio_reset(iport->fnic);
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	list_for_each_entry_safe(tport, next, &iport->tport_list, links) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "removing rport: 0x%x", tport->fcid);
		fdls_delete_tport(iport, tport);
	}

	if ((fnic_fdmi_support == 1) && (iport->fabric.fdmi_pending > 0)) {
		del_timer_sync(&iport->fabric.fdmi_timer);
		iport->fabric.fdmi_pending = 0;
	}

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "0x%x: FDLS finish processing link down", iport->fcid);
}
