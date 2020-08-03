// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
 */

#include "bfad_drv.h"
#include "bfa_modules.h"

BFA_TRC_FILE(HAL, FCPIM);

/*
 *  BFA ITNIM Related definitions
 */
static void bfa_itnim_update_del_itn_stats(struct bfa_itnim_s *itnim);

#define BFA_ITNIM_FROM_TAG(_fcpim, _tag)                                \
	(((_fcpim)->itnim_arr + ((_tag) & ((_fcpim)->num_itnims - 1))))

#define bfa_fcpim_additn(__itnim)					\
	list_add_tail(&(__itnim)->qe, &(__itnim)->fcpim->itnim_q)
#define bfa_fcpim_delitn(__itnim)	do {				\
	WARN_ON(!bfa_q_is_on_q(&(__itnim)->fcpim->itnim_q, __itnim));   \
	bfa_itnim_update_del_itn_stats(__itnim);      \
	list_del(&(__itnim)->qe);      \
	WARN_ON(!list_empty(&(__itnim)->io_q));				\
	WARN_ON(!list_empty(&(__itnim)->io_cleanup_q));			\
	WARN_ON(!list_empty(&(__itnim)->pending_q));			\
} while (0)

#define bfa_itnim_online_cb(__itnim) do {				\
	if ((__itnim)->bfa->fcs)					\
		bfa_cb_itnim_online((__itnim)->ditn);      \
	else {								\
		bfa_cb_queue((__itnim)->bfa, &(__itnim)->hcb_qe,	\
		__bfa_cb_itnim_online, (__itnim));      \
	}								\
} while (0)

#define bfa_itnim_offline_cb(__itnim) do {				\
	if ((__itnim)->bfa->fcs)					\
		bfa_cb_itnim_offline((__itnim)->ditn);      \
	else {								\
		bfa_cb_queue((__itnim)->bfa, &(__itnim)->hcb_qe,	\
		__bfa_cb_itnim_offline, (__itnim));      \
	}								\
} while (0)

#define bfa_itnim_sler_cb(__itnim) do {					\
	if ((__itnim)->bfa->fcs)					\
		bfa_cb_itnim_sler((__itnim)->ditn);      \
	else {								\
		bfa_cb_queue((__itnim)->bfa, &(__itnim)->hcb_qe,	\
		__bfa_cb_itnim_sler, (__itnim));      \
	}								\
} while (0)

enum bfa_ioim_lm_ua_status {
	BFA_IOIM_LM_UA_RESET = 0,
	BFA_IOIM_LM_UA_SET = 1,
};

/*
 *  itnim state machine event
 */
enum bfa_itnim_event {
	BFA_ITNIM_SM_CREATE = 1,	/*  itnim is created */
	BFA_ITNIM_SM_ONLINE = 2,	/*  itnim is online */
	BFA_ITNIM_SM_OFFLINE = 3,	/*  itnim is offline */
	BFA_ITNIM_SM_FWRSP = 4,		/*  firmware response */
	BFA_ITNIM_SM_DELETE = 5,	/*  deleting an existing itnim */
	BFA_ITNIM_SM_CLEANUP = 6,	/*  IO cleanup completion */
	BFA_ITNIM_SM_SLER = 7,		/*  second level error recovery */
	BFA_ITNIM_SM_HWFAIL = 8,	/*  IOC h/w failure event */
	BFA_ITNIM_SM_QRESUME = 9,	/*  queue space available */
};

/*
 *  BFA IOIM related definitions
 */
#define bfa_ioim_move_to_comp_q(__ioim) do {				\
	list_del(&(__ioim)->qe);					\
	list_add_tail(&(__ioim)->qe, &(__ioim)->fcpim->ioim_comp_q);	\
} while (0)


#define bfa_ioim_cb_profile_comp(__fcpim, __ioim) do {			\
	if ((__fcpim)->profile_comp)					\
		(__fcpim)->profile_comp(__ioim);			\
} while (0)

#define bfa_ioim_cb_profile_start(__fcpim, __ioim) do {			\
	if ((__fcpim)->profile_start)					\
		(__fcpim)->profile_start(__ioim);			\
} while (0)

/*
 * IO state machine events
 */
enum bfa_ioim_event {
	BFA_IOIM_SM_START	= 1,	/*  io start request from host */
	BFA_IOIM_SM_COMP_GOOD	= 2,	/*  io good comp, resource free */
	BFA_IOIM_SM_COMP	= 3,	/*  io comp, resource is free */
	BFA_IOIM_SM_COMP_UTAG	= 4,	/*  io comp, resource is free */
	BFA_IOIM_SM_DONE	= 5,	/*  io comp, resource not free */
	BFA_IOIM_SM_FREE	= 6,	/*  io resource is freed */
	BFA_IOIM_SM_ABORT	= 7,	/*  abort request from scsi stack */
	BFA_IOIM_SM_ABORT_COMP	= 8,	/*  abort from f/w */
	BFA_IOIM_SM_ABORT_DONE	= 9,	/*  abort completion from f/w */
	BFA_IOIM_SM_QRESUME	= 10,	/*  CQ space available to queue IO */
	BFA_IOIM_SM_SGALLOCED	= 11,	/*  SG page allocation successful */
	BFA_IOIM_SM_SQRETRY	= 12,	/*  sequence recovery retry */
	BFA_IOIM_SM_HCB		= 13,	/*  bfa callback complete */
	BFA_IOIM_SM_CLEANUP	= 14,	/*  IO cleanup from itnim */
	BFA_IOIM_SM_TMSTART	= 15,	/*  IO cleanup from tskim */
	BFA_IOIM_SM_TMDONE	= 16,	/*  IO cleanup from tskim */
	BFA_IOIM_SM_HWFAIL	= 17,	/*  IOC h/w failure event */
	BFA_IOIM_SM_IOTOV	= 18,	/*  ITN offline TOV */
};


/*
 *  BFA TSKIM related definitions
 */

/*
 * task management completion handling
 */
#define bfa_tskim_qcomp(__tskim, __cbfn) do {				\
	bfa_cb_queue((__tskim)->bfa, &(__tskim)->hcb_qe, __cbfn, (__tskim));\
	bfa_tskim_notify_comp(__tskim);      \
} while (0)

#define bfa_tskim_notify_comp(__tskim) do {				\
	if ((__tskim)->notify)						\
		bfa_itnim_tskdone((__tskim)->itnim);      \
} while (0)


enum bfa_tskim_event {
	BFA_TSKIM_SM_START	= 1,	/*  TM command start		*/
	BFA_TSKIM_SM_DONE	= 2,	/*  TM completion		*/
	BFA_TSKIM_SM_QRESUME	= 3,	/*  resume after qfull		*/
	BFA_TSKIM_SM_HWFAIL	= 5,	/*  IOC h/w failure event	*/
	BFA_TSKIM_SM_HCB	= 6,	/*  BFA callback completion	*/
	BFA_TSKIM_SM_IOS_DONE	= 7,	/*  IO and sub TM completions	*/
	BFA_TSKIM_SM_CLEANUP	= 8,	/*  TM cleanup on ITN offline	*/
	BFA_TSKIM_SM_CLEANUP_DONE = 9,	/*  TM abort completion	*/
	BFA_TSKIM_SM_UTAG	= 10,	/*  TM completion unknown tag  */
};

/*
 * forward declaration for BFA ITNIM functions
 */
static void     bfa_itnim_iocdisable_cleanup(struct bfa_itnim_s *itnim);
static bfa_boolean_t bfa_itnim_send_fwcreate(struct bfa_itnim_s *itnim);
static bfa_boolean_t bfa_itnim_send_fwdelete(struct bfa_itnim_s *itnim);
static void     bfa_itnim_cleanp_comp(void *itnim_cbarg);
static void     bfa_itnim_cleanup(struct bfa_itnim_s *itnim);
static void     __bfa_cb_itnim_online(void *cbarg, bfa_boolean_t complete);
static void     __bfa_cb_itnim_offline(void *cbarg, bfa_boolean_t complete);
static void     __bfa_cb_itnim_sler(void *cbarg, bfa_boolean_t complete);
static void     bfa_itnim_iotov_online(struct bfa_itnim_s *itnim);
static void     bfa_itnim_iotov_cleanup(struct bfa_itnim_s *itnim);
static void     bfa_itnim_iotov(void *itnim_arg);
static void     bfa_itnim_iotov_start(struct bfa_itnim_s *itnim);
static void     bfa_itnim_iotov_stop(struct bfa_itnim_s *itnim);
static void     bfa_itnim_iotov_delete(struct bfa_itnim_s *itnim);

/*
 * forward declaration of ITNIM state machine
 */
static void     bfa_itnim_sm_uninit(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_created(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_fwcreate(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_delete_pending(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_online(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_sler(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_cleanup_offline(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_cleanup_delete(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_fwdelete(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_offline(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_iocdisable(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_deleting(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_fwcreate_qfull(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_fwdelete_qfull(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);
static void     bfa_itnim_sm_deleting_qfull(struct bfa_itnim_s *itnim,
					enum bfa_itnim_event event);

/*
 * forward declaration for BFA IOIM functions
 */
static bfa_boolean_t	bfa_ioim_send_ioreq(struct bfa_ioim_s *ioim);
static bfa_boolean_t	bfa_ioim_sgpg_alloc(struct bfa_ioim_s *ioim);
static bfa_boolean_t	bfa_ioim_send_abort(struct bfa_ioim_s *ioim);
static void		bfa_ioim_notify_cleanup(struct bfa_ioim_s *ioim);
static void __bfa_cb_ioim_good_comp(void *cbarg, bfa_boolean_t complete);
static void __bfa_cb_ioim_comp(void *cbarg, bfa_boolean_t complete);
static void __bfa_cb_ioim_abort(void *cbarg, bfa_boolean_t complete);
static void __bfa_cb_ioim_failed(void *cbarg, bfa_boolean_t complete);
static void __bfa_cb_ioim_pathtov(void *cbarg, bfa_boolean_t complete);
static bfa_boolean_t    bfa_ioim_is_abortable(struct bfa_ioim_s *ioim);

/*
 * forward declaration of BFA IO state machine
 */
static void     bfa_ioim_sm_uninit(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_sgalloc(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_active(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_abort(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_cleanup(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_qfull(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_abort_qfull(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_cleanup_qfull(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_hcb(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_hcb_free(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void     bfa_ioim_sm_resfree(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
static void	bfa_ioim_sm_cmnd_retry(struct bfa_ioim_s *ioim,
					enum bfa_ioim_event event);
/*
 * forward declaration for BFA TSKIM functions
 */
static void     __bfa_cb_tskim_done(void *cbarg, bfa_boolean_t complete);
static void     __bfa_cb_tskim_failed(void *cbarg, bfa_boolean_t complete);
static bfa_boolean_t bfa_tskim_match_scope(struct bfa_tskim_s *tskim,
					struct scsi_lun lun);
static void     bfa_tskim_gather_ios(struct bfa_tskim_s *tskim);
static void     bfa_tskim_cleanp_comp(void *tskim_cbarg);
static void     bfa_tskim_cleanup_ios(struct bfa_tskim_s *tskim);
static bfa_boolean_t bfa_tskim_send(struct bfa_tskim_s *tskim);
static bfa_boolean_t bfa_tskim_send_abort(struct bfa_tskim_s *tskim);
static void     bfa_tskim_iocdisable_ios(struct bfa_tskim_s *tskim);

/*
 * forward declaration of BFA TSKIM state machine
 */
static void     bfa_tskim_sm_uninit(struct bfa_tskim_s *tskim,
					enum bfa_tskim_event event);
static void     bfa_tskim_sm_active(struct bfa_tskim_s *tskim,
					enum bfa_tskim_event event);
static void     bfa_tskim_sm_cleanup(struct bfa_tskim_s *tskim,
					enum bfa_tskim_event event);
static void     bfa_tskim_sm_iocleanup(struct bfa_tskim_s *tskim,
					enum bfa_tskim_event event);
static void     bfa_tskim_sm_qfull(struct bfa_tskim_s *tskim,
					enum bfa_tskim_event event);
static void     bfa_tskim_sm_cleanup_qfull(struct bfa_tskim_s *tskim,
					enum bfa_tskim_event event);
static void     bfa_tskim_sm_hcb(struct bfa_tskim_s *tskim,
					enum bfa_tskim_event event);
/*
 *  BFA FCP Initiator Mode module
 */

/*
 * Compute and return memory needed by FCP(im) module.
 */
static void
bfa_fcpim_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len)
{
	bfa_itnim_meminfo(cfg, km_len);

	/*
	 * IO memory
	 */
	*km_len += cfg->fwcfg.num_ioim_reqs *
	  (sizeof(struct bfa_ioim_s) + sizeof(struct bfa_ioim_sp_s));

	/*
	 * task management command memory
	 */
	if (cfg->fwcfg.num_tskim_reqs < BFA_TSKIM_MIN)
		cfg->fwcfg.num_tskim_reqs = BFA_TSKIM_MIN;
	*km_len += cfg->fwcfg.num_tskim_reqs * sizeof(struct bfa_tskim_s);
}


static void
bfa_fcpim_attach(struct bfa_fcp_mod_s *fcp, void *bfad,
		struct bfa_iocfc_cfg_s *cfg, struct bfa_pcidev_s *pcidev)
{
	struct bfa_fcpim_s *fcpim = &fcp->fcpim;
	struct bfa_s *bfa = fcp->bfa;

	bfa_trc(bfa, cfg->drvcfg.path_tov);
	bfa_trc(bfa, cfg->fwcfg.num_rports);
	bfa_trc(bfa, cfg->fwcfg.num_ioim_reqs);
	bfa_trc(bfa, cfg->fwcfg.num_tskim_reqs);

	fcpim->fcp		= fcp;
	fcpim->bfa		= bfa;
	fcpim->num_itnims	= cfg->fwcfg.num_rports;
	fcpim->num_tskim_reqs = cfg->fwcfg.num_tskim_reqs;
	fcpim->path_tov		= cfg->drvcfg.path_tov;
	fcpim->delay_comp	= cfg->drvcfg.delay_comp;
	fcpim->profile_comp = NULL;
	fcpim->profile_start = NULL;

	bfa_itnim_attach(fcpim);
	bfa_tskim_attach(fcpim);
	bfa_ioim_attach(fcpim);
}

void
bfa_fcpim_iocdisable(struct bfa_fcp_mod_s *fcp)
{
	struct bfa_fcpim_s *fcpim = &fcp->fcpim;
	struct bfa_itnim_s *itnim;
	struct list_head *qe, *qen;

	/* Enqueue unused ioim resources to free_q */
	list_splice_tail_init(&fcpim->tskim_unused_q, &fcpim->tskim_free_q);

	list_for_each_safe(qe, qen, &fcpim->itnim_q) {
		itnim = (struct bfa_itnim_s *) qe;
		bfa_itnim_iocdisable(itnim);
	}
}

void
bfa_fcpim_path_tov_set(struct bfa_s *bfa, u16 path_tov)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);

	fcpim->path_tov = path_tov * 1000;
	if (fcpim->path_tov > BFA_FCPIM_PATHTOV_MAX)
		fcpim->path_tov = BFA_FCPIM_PATHTOV_MAX;
}

u16
bfa_fcpim_path_tov_get(struct bfa_s *bfa)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);

	return fcpim->path_tov / 1000;
}

#define bfa_fcpim_add_iostats(__l, __r, __stats)	\
	(__l->__stats += __r->__stats)

void
bfa_fcpim_add_stats(struct bfa_itnim_iostats_s *lstats,
		struct bfa_itnim_iostats_s *rstats)
{
	bfa_fcpim_add_iostats(lstats, rstats, total_ios);
	bfa_fcpim_add_iostats(lstats, rstats, qresumes);
	bfa_fcpim_add_iostats(lstats, rstats, no_iotags);
	bfa_fcpim_add_iostats(lstats, rstats, io_aborts);
	bfa_fcpim_add_iostats(lstats, rstats, no_tskims);
	bfa_fcpim_add_iostats(lstats, rstats, iocomp_ok);
	bfa_fcpim_add_iostats(lstats, rstats, iocomp_underrun);
	bfa_fcpim_add_iostats(lstats, rstats, iocomp_overrun);
	bfa_fcpim_add_iostats(lstats, rstats, iocomp_aborted);
	bfa_fcpim_add_iostats(lstats, rstats, iocomp_timedout);
	bfa_fcpim_add_iostats(lstats, rstats, iocom_nexus_abort);
	bfa_fcpim_add_iostats(lstats, rstats, iocom_proto_err);
	bfa_fcpim_add_iostats(lstats, rstats, iocom_dif_err);
	bfa_fcpim_add_iostats(lstats, rstats, iocom_sqer_needed);
	bfa_fcpim_add_iostats(lstats, rstats, iocom_res_free);
	bfa_fcpim_add_iostats(lstats, rstats, iocom_hostabrts);
	bfa_fcpim_add_iostats(lstats, rstats, iocom_utags);
	bfa_fcpim_add_iostats(lstats, rstats, io_cleanups);
	bfa_fcpim_add_iostats(lstats, rstats, io_tmaborts);
	bfa_fcpim_add_iostats(lstats, rstats, onlines);
	bfa_fcpim_add_iostats(lstats, rstats, offlines);
	bfa_fcpim_add_iostats(lstats, rstats, creates);
	bfa_fcpim_add_iostats(lstats, rstats, deletes);
	bfa_fcpim_add_iostats(lstats, rstats, create_comps);
	bfa_fcpim_add_iostats(lstats, rstats, delete_comps);
	bfa_fcpim_add_iostats(lstats, rstats, sler_events);
	bfa_fcpim_add_iostats(lstats, rstats, fw_create);
	bfa_fcpim_add_iostats(lstats, rstats, fw_delete);
	bfa_fcpim_add_iostats(lstats, rstats, ioc_disabled);
	bfa_fcpim_add_iostats(lstats, rstats, cleanup_comps);
	bfa_fcpim_add_iostats(lstats, rstats, tm_cmnds);
	bfa_fcpim_add_iostats(lstats, rstats, tm_fw_rsps);
	bfa_fcpim_add_iostats(lstats, rstats, tm_success);
	bfa_fcpim_add_iostats(lstats, rstats, tm_failures);
	bfa_fcpim_add_iostats(lstats, rstats, tm_io_comps);
	bfa_fcpim_add_iostats(lstats, rstats, tm_qresumes);
	bfa_fcpim_add_iostats(lstats, rstats, tm_iocdowns);
	bfa_fcpim_add_iostats(lstats, rstats, tm_cleanups);
	bfa_fcpim_add_iostats(lstats, rstats, tm_cleanup_comps);
	bfa_fcpim_add_iostats(lstats, rstats, io_comps);
	bfa_fcpim_add_iostats(lstats, rstats, input_reqs);
	bfa_fcpim_add_iostats(lstats, rstats, output_reqs);
	bfa_fcpim_add_iostats(lstats, rstats, rd_throughput);
	bfa_fcpim_add_iostats(lstats, rstats, wr_throughput);
}

bfa_status_t
bfa_fcpim_port_iostats(struct bfa_s *bfa,
		struct bfa_itnim_iostats_s *stats, u8 lp_tag)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	struct list_head *qe, *qen;
	struct bfa_itnim_s *itnim;

	/* accumulate IO stats from itnim */
	memset(stats, 0, sizeof(struct bfa_itnim_iostats_s));
	list_for_each_safe(qe, qen, &fcpim->itnim_q) {
		itnim = (struct bfa_itnim_s *) qe;
		if (itnim->rport->rport_info.lp_tag != lp_tag)
			continue;
		bfa_fcpim_add_stats(stats, &(itnim->stats));
	}
	return BFA_STATUS_OK;
}

static void
bfa_ioim_profile_comp(struct bfa_ioim_s *ioim)
{
	struct bfa_itnim_latency_s *io_lat =
			&(ioim->itnim->ioprofile.io_latency);
	u32 val, idx;

	val = (u32)(jiffies - ioim->start_time);
	idx = bfa_ioim_get_index(scsi_bufflen((struct scsi_cmnd *)ioim->dio));
	bfa_itnim_ioprofile_update(ioim->itnim, idx);

	io_lat->count[idx]++;
	io_lat->min[idx] = (io_lat->min[idx] < val) ? io_lat->min[idx] : val;
	io_lat->max[idx] = (io_lat->max[idx] > val) ? io_lat->max[idx] : val;
	io_lat->avg[idx] += val;
}

static void
bfa_ioim_profile_start(struct bfa_ioim_s *ioim)
{
	ioim->start_time = jiffies;
}

bfa_status_t
bfa_fcpim_profile_on(struct bfa_s *bfa, time64_t time)
{
	struct bfa_itnim_s *itnim;
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	struct list_head *qe, *qen;

	/* accumulate IO stats from itnim */
	list_for_each_safe(qe, qen, &fcpim->itnim_q) {
		itnim = (struct bfa_itnim_s *) qe;
		bfa_itnim_clear_stats(itnim);
	}
	fcpim->io_profile = BFA_TRUE;
	fcpim->io_profile_start_time = time;
	fcpim->profile_comp = bfa_ioim_profile_comp;
	fcpim->profile_start = bfa_ioim_profile_start;
	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcpim_profile_off(struct bfa_s *bfa)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	fcpim->io_profile = BFA_FALSE;
	fcpim->io_profile_start_time = 0;
	fcpim->profile_comp = NULL;
	fcpim->profile_start = NULL;
	return BFA_STATUS_OK;
}

u16
bfa_fcpim_qdepth_get(struct bfa_s *bfa)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);

	return fcpim->q_depth;
}

/*
 *  BFA ITNIM module state machine functions
 */

/*
 * Beginning/unallocated state - no events expected.
 */
static void
bfa_itnim_sm_uninit(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_CREATE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_created);
		itnim->is_online = BFA_FALSE;
		bfa_fcpim_additn(itnim);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Beginning state, only online event expected.
 */
static void
bfa_itnim_sm_created(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_ONLINE:
		if (bfa_itnim_send_fwcreate(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate_qfull);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_fcpim_delitn(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 *	Waiting for itnim create response from firmware.
 */
static void
bfa_itnim_sm_fwcreate(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_FWRSP:
		bfa_sm_set_state(itnim, bfa_itnim_sm_online);
		itnim->is_online = BFA_TRUE;
		bfa_itnim_iotov_online(itnim);
		bfa_itnim_online_cb(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_delete_pending);
		break;

	case BFA_ITNIM_SM_OFFLINE:
		if (bfa_itnim_send_fwdelete(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

static void
bfa_itnim_sm_fwcreate_qfull(struct bfa_itnim_s *itnim,
			enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_QRESUME:
		bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate);
		bfa_itnim_send_fwcreate(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		bfa_fcpim_delitn(itnim);
		break;

	case BFA_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_offline);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		bfa_itnim_offline_cb(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Waiting for itnim create response from firmware, a delete is pending.
 */
static void
bfa_itnim_sm_delete_pending(struct bfa_itnim_s *itnim,
				enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_FWRSP:
		if (bfa_itnim_send_fwdelete(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_deleting);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_deleting_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_fcpim_delitn(itnim);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Online state - normal parking state.
 */
static void
bfa_itnim_sm_online(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_offline);
		itnim->is_online = BFA_FALSE;
		bfa_itnim_iotov_start(itnim);
		bfa_itnim_cleanup(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_delete);
		itnim->is_online = BFA_FALSE;
		bfa_itnim_cleanup(itnim);
		break;

	case BFA_ITNIM_SM_SLER:
		bfa_sm_set_state(itnim, bfa_itnim_sm_sler);
		itnim->is_online = BFA_FALSE;
		bfa_itnim_iotov_start(itnim);
		bfa_itnim_sler_cb(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		itnim->is_online = BFA_FALSE;
		bfa_itnim_iotov_start(itnim);
		bfa_itnim_iocdisable_cleanup(itnim);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Second level error recovery need.
 */
static void
bfa_itnim_sm_sler(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_offline);
		bfa_itnim_cleanup(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_delete);
		bfa_itnim_cleanup(itnim);
		bfa_itnim_iotov_delete(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_itnim_iocdisable_cleanup(itnim);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Going offline. Waiting for active IO cleanup.
 */
static void
bfa_itnim_sm_cleanup_offline(struct bfa_itnim_s *itnim,
				 enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_CLEANUP:
		if (bfa_itnim_send_fwdelete(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete_qfull);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_cleanup_delete);
		bfa_itnim_iotov_delete(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_itnim_iocdisable_cleanup(itnim);
		bfa_itnim_offline_cb(itnim);
		break;

	case BFA_ITNIM_SM_SLER:
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Deleting itnim. Waiting for active IO cleanup.
 */
static void
bfa_itnim_sm_cleanup_delete(struct bfa_itnim_s *itnim,
				enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_CLEANUP:
		if (bfa_itnim_send_fwdelete(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_deleting);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_deleting_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_itnim_iocdisable_cleanup(itnim);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Rport offline. Fimrware itnim is being deleted - awaiting f/w response.
 */
static void
bfa_itnim_sm_fwdelete(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_FWRSP:
		bfa_sm_set_state(itnim, bfa_itnim_sm_offline);
		bfa_itnim_offline_cb(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_deleting);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_itnim_offline_cb(itnim);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

static void
bfa_itnim_sm_fwdelete_qfull(struct bfa_itnim_s *itnim,
			enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_QRESUME:
		bfa_sm_set_state(itnim, bfa_itnim_sm_fwdelete);
		bfa_itnim_send_fwdelete(itnim);
		break;

	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_deleting_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		bfa_itnim_offline_cb(itnim);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Offline state.
 */
static void
bfa_itnim_sm_offline(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_itnim_iotov_delete(itnim);
		bfa_fcpim_delitn(itnim);
		break;

	case BFA_ITNIM_SM_ONLINE:
		if (bfa_itnim_send_fwcreate(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_iocdisable);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

static void
bfa_itnim_sm_iocdisable(struct bfa_itnim_s *itnim,
				enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_itnim_iotov_delete(itnim);
		bfa_fcpim_delitn(itnim);
		break;

	case BFA_ITNIM_SM_OFFLINE:
		bfa_itnim_offline_cb(itnim);
		break;

	case BFA_ITNIM_SM_ONLINE:
		if (bfa_itnim_send_fwcreate(itnim))
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate);
		else
			bfa_sm_set_state(itnim, bfa_itnim_sm_fwcreate_qfull);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Itnim is deleted, waiting for firmware response to delete.
 */
static void
bfa_itnim_sm_deleting(struct bfa_itnim_s *itnim, enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_FWRSP:
	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_fcpim_delitn(itnim);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

static void
bfa_itnim_sm_deleting_qfull(struct bfa_itnim_s *itnim,
		enum bfa_itnim_event event)
{
	bfa_trc(itnim->bfa, itnim->rport->rport_tag);
	bfa_trc(itnim->bfa, event);

	switch (event) {
	case BFA_ITNIM_SM_QRESUME:
		bfa_sm_set_state(itnim, bfa_itnim_sm_deleting);
		bfa_itnim_send_fwdelete(itnim);
		break;

	case BFA_ITNIM_SM_HWFAIL:
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
		bfa_reqq_wcancel(&itnim->reqq_wait);
		bfa_fcpim_delitn(itnim);
		break;

	default:
		bfa_sm_fault(itnim->bfa, event);
	}
}

/*
 * Initiate cleanup of all IOs on an IOC failure.
 */
static void
bfa_itnim_iocdisable_cleanup(struct bfa_itnim_s *itnim)
{
	struct bfa_tskim_s *tskim;
	struct bfa_ioim_s *ioim;
	struct list_head	*qe, *qen;

	list_for_each_safe(qe, qen, &itnim->tsk_q) {
		tskim = (struct bfa_tskim_s *) qe;
		bfa_tskim_iocdisable(tskim);
	}

	list_for_each_safe(qe, qen, &itnim->io_q) {
		ioim = (struct bfa_ioim_s *) qe;
		bfa_ioim_iocdisable(ioim);
	}

	/*
	 * For IO request in pending queue, we pretend an early timeout.
	 */
	list_for_each_safe(qe, qen, &itnim->pending_q) {
		ioim = (struct bfa_ioim_s *) qe;
		bfa_ioim_tov(ioim);
	}

	list_for_each_safe(qe, qen, &itnim->io_cleanup_q) {
		ioim = (struct bfa_ioim_s *) qe;
		bfa_ioim_iocdisable(ioim);
	}
}

/*
 * IO cleanup completion
 */
static void
bfa_itnim_cleanp_comp(void *itnim_cbarg)
{
	struct bfa_itnim_s *itnim = itnim_cbarg;

	bfa_stats(itnim, cleanup_comps);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_CLEANUP);
}

/*
 * Initiate cleanup of all IOs.
 */
static void
bfa_itnim_cleanup(struct bfa_itnim_s *itnim)
{
	struct bfa_ioim_s  *ioim;
	struct bfa_tskim_s *tskim;
	struct list_head	*qe, *qen;

	bfa_wc_init(&itnim->wc, bfa_itnim_cleanp_comp, itnim);

	list_for_each_safe(qe, qen, &itnim->io_q) {
		ioim = (struct bfa_ioim_s *) qe;

		/*
		 * Move IO to a cleanup queue from active queue so that a later
		 * TM will not pickup this IO.
		 */
		list_del(&ioim->qe);
		list_add_tail(&ioim->qe, &itnim->io_cleanup_q);

		bfa_wc_up(&itnim->wc);
		bfa_ioim_cleanup(ioim);
	}

	list_for_each_safe(qe, qen, &itnim->tsk_q) {
		tskim = (struct bfa_tskim_s *) qe;
		bfa_wc_up(&itnim->wc);
		bfa_tskim_cleanup(tskim);
	}

	bfa_wc_wait(&itnim->wc);
}

static void
__bfa_cb_itnim_online(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_itnim_s *itnim = cbarg;

	if (complete)
		bfa_cb_itnim_online(itnim->ditn);
}

static void
__bfa_cb_itnim_offline(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_itnim_s *itnim = cbarg;

	if (complete)
		bfa_cb_itnim_offline(itnim->ditn);
}

static void
__bfa_cb_itnim_sler(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_itnim_s *itnim = cbarg;

	if (complete)
		bfa_cb_itnim_sler(itnim->ditn);
}

/*
 * Call to resume any I/O requests waiting for room in request queue.
 */
static void
bfa_itnim_qresume(void *cbarg)
{
	struct bfa_itnim_s *itnim = cbarg;

	bfa_sm_send_event(itnim, BFA_ITNIM_SM_QRESUME);
}

/*
 *  bfa_itnim_public
 */

void
bfa_itnim_iodone(struct bfa_itnim_s *itnim)
{
	bfa_wc_down(&itnim->wc);
}

void
bfa_itnim_tskdone(struct bfa_itnim_s *itnim)
{
	bfa_wc_down(&itnim->wc);
}

void
bfa_itnim_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len)
{
	/*
	 * ITN memory
	 */
	*km_len += cfg->fwcfg.num_rports * sizeof(struct bfa_itnim_s);
}

void
bfa_itnim_attach(struct bfa_fcpim_s *fcpim)
{
	struct bfa_s	*bfa = fcpim->bfa;
	struct bfa_fcp_mod_s	*fcp = fcpim->fcp;
	struct bfa_itnim_s *itnim;
	int	i, j;

	INIT_LIST_HEAD(&fcpim->itnim_q);

	itnim = (struct bfa_itnim_s *) bfa_mem_kva_curp(fcp);
	fcpim->itnim_arr = itnim;

	for (i = 0; i < fcpim->num_itnims; i++, itnim++) {
		memset(itnim, 0, sizeof(struct bfa_itnim_s));
		itnim->bfa = bfa;
		itnim->fcpim = fcpim;
		itnim->reqq = BFA_REQQ_QOS_LO;
		itnim->rport = BFA_RPORT_FROM_TAG(bfa, i);
		itnim->iotov_active = BFA_FALSE;
		bfa_reqq_winit(&itnim->reqq_wait, bfa_itnim_qresume, itnim);

		INIT_LIST_HEAD(&itnim->io_q);
		INIT_LIST_HEAD(&itnim->io_cleanup_q);
		INIT_LIST_HEAD(&itnim->pending_q);
		INIT_LIST_HEAD(&itnim->tsk_q);
		INIT_LIST_HEAD(&itnim->delay_comp_q);
		for (j = 0; j < BFA_IOBUCKET_MAX; j++)
			itnim->ioprofile.io_latency.min[j] = ~0;
		bfa_sm_set_state(itnim, bfa_itnim_sm_uninit);
	}

	bfa_mem_kva_curp(fcp) = (u8 *) itnim;
}

void
bfa_itnim_iocdisable(struct bfa_itnim_s *itnim)
{
	bfa_stats(itnim, ioc_disabled);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_HWFAIL);
}

static bfa_boolean_t
bfa_itnim_send_fwcreate(struct bfa_itnim_s *itnim)
{
	struct bfi_itn_create_req_s *m;

	itnim->msg_no++;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(itnim->bfa, itnim->reqq);
	if (!m) {
		bfa_reqq_wait(itnim->bfa, itnim->reqq, &itnim->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_ITN, BFI_ITN_H2I_CREATE_REQ,
			bfa_fn_lpu(itnim->bfa));
	m->fw_handle = itnim->rport->fw_handle;
	m->class = FC_CLASS_3;
	m->seq_rec = itnim->seq_rec;
	m->msg_no = itnim->msg_no;
	bfa_stats(itnim, fw_create);

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(itnim->bfa, itnim->reqq, m->mh);
	return BFA_TRUE;
}

static bfa_boolean_t
bfa_itnim_send_fwdelete(struct bfa_itnim_s *itnim)
{
	struct bfi_itn_delete_req_s *m;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(itnim->bfa, itnim->reqq);
	if (!m) {
		bfa_reqq_wait(itnim->bfa, itnim->reqq, &itnim->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_ITN, BFI_ITN_H2I_DELETE_REQ,
			bfa_fn_lpu(itnim->bfa));
	m->fw_handle = itnim->rport->fw_handle;
	bfa_stats(itnim, fw_delete);

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(itnim->bfa, itnim->reqq, m->mh);
	return BFA_TRUE;
}

/*
 * Cleanup all pending failed inflight requests.
 */
static void
bfa_itnim_delayed_comp(struct bfa_itnim_s *itnim, bfa_boolean_t iotov)
{
	struct bfa_ioim_s *ioim;
	struct list_head *qe, *qen;

	list_for_each_safe(qe, qen, &itnim->delay_comp_q) {
		ioim = (struct bfa_ioim_s *)qe;
		bfa_ioim_delayed_comp(ioim, iotov);
	}
}

/*
 * Start all pending IO requests.
 */
static void
bfa_itnim_iotov_online(struct bfa_itnim_s *itnim)
{
	struct bfa_ioim_s *ioim;

	bfa_itnim_iotov_stop(itnim);

	/*
	 * Abort all inflight IO requests in the queue
	 */
	bfa_itnim_delayed_comp(itnim, BFA_FALSE);

	/*
	 * Start all pending IO requests.
	 */
	while (!list_empty(&itnim->pending_q)) {
		bfa_q_deq(&itnim->pending_q, &ioim);
		list_add_tail(&ioim->qe, &itnim->io_q);
		bfa_ioim_start(ioim);
	}
}

/*
 * Fail all pending IO requests
 */
static void
bfa_itnim_iotov_cleanup(struct bfa_itnim_s *itnim)
{
	struct bfa_ioim_s *ioim;

	/*
	 * Fail all inflight IO requests in the queue
	 */
	bfa_itnim_delayed_comp(itnim, BFA_TRUE);

	/*
	 * Fail any pending IO requests.
	 */
	while (!list_empty(&itnim->pending_q)) {
		bfa_q_deq(&itnim->pending_q, &ioim);
		list_add_tail(&ioim->qe, &ioim->fcpim->ioim_comp_q);
		bfa_ioim_tov(ioim);
	}
}

/*
 * IO TOV timer callback. Fail any pending IO requests.
 */
static void
bfa_itnim_iotov(void *itnim_arg)
{
	struct bfa_itnim_s *itnim = itnim_arg;

	itnim->iotov_active = BFA_FALSE;

	bfa_cb_itnim_tov_begin(itnim->ditn);
	bfa_itnim_iotov_cleanup(itnim);
	bfa_cb_itnim_tov(itnim->ditn);
}

/*
 * Start IO TOV timer for failing back pending IO requests in offline state.
 */
static void
bfa_itnim_iotov_start(struct bfa_itnim_s *itnim)
{
	if (itnim->fcpim->path_tov > 0) {

		itnim->iotov_active = BFA_TRUE;
		WARN_ON(!bfa_itnim_hold_io(itnim));
		bfa_timer_start(itnim->bfa, &itnim->timer,
			bfa_itnim_iotov, itnim, itnim->fcpim->path_tov);
	}
}

/*
 * Stop IO TOV timer.
 */
static void
bfa_itnim_iotov_stop(struct bfa_itnim_s *itnim)
{
	if (itnim->iotov_active) {
		itnim->iotov_active = BFA_FALSE;
		bfa_timer_stop(&itnim->timer);
	}
}

/*
 * Stop IO TOV timer.
 */
static void
bfa_itnim_iotov_delete(struct bfa_itnim_s *itnim)
{
	bfa_boolean_t pathtov_active = BFA_FALSE;

	if (itnim->iotov_active)
		pathtov_active = BFA_TRUE;

	bfa_itnim_iotov_stop(itnim);
	if (pathtov_active)
		bfa_cb_itnim_tov_begin(itnim->ditn);
	bfa_itnim_iotov_cleanup(itnim);
	if (pathtov_active)
		bfa_cb_itnim_tov(itnim->ditn);
}

static void
bfa_itnim_update_del_itn_stats(struct bfa_itnim_s *itnim)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(itnim->bfa);
	fcpim->del_itn_stats.del_itn_iocomp_aborted +=
		itnim->stats.iocomp_aborted;
	fcpim->del_itn_stats.del_itn_iocomp_timedout +=
		itnim->stats.iocomp_timedout;
	fcpim->del_itn_stats.del_itn_iocom_sqer_needed +=
		itnim->stats.iocom_sqer_needed;
	fcpim->del_itn_stats.del_itn_iocom_res_free +=
		itnim->stats.iocom_res_free;
	fcpim->del_itn_stats.del_itn_iocom_hostabrts +=
		itnim->stats.iocom_hostabrts;
	fcpim->del_itn_stats.del_itn_total_ios += itnim->stats.total_ios;
	fcpim->del_itn_stats.del_io_iocdowns += itnim->stats.io_iocdowns;
	fcpim->del_itn_stats.del_tm_iocdowns += itnim->stats.tm_iocdowns;
}

/*
 * bfa_itnim_public
 */

/*
 * Itnim interrupt processing.
 */
void
bfa_itnim_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	union bfi_itn_i2h_msg_u msg;
	struct bfa_itnim_s *itnim;

	bfa_trc(bfa, m->mhdr.msg_id);

	msg.msg = m;

	switch (m->mhdr.msg_id) {
	case BFI_ITN_I2H_CREATE_RSP:
		itnim = BFA_ITNIM_FROM_TAG(fcpim,
						msg.create_rsp->bfa_handle);
		WARN_ON(msg.create_rsp->status != BFA_STATUS_OK);
		bfa_stats(itnim, create_comps);
		bfa_sm_send_event(itnim, BFA_ITNIM_SM_FWRSP);
		break;

	case BFI_ITN_I2H_DELETE_RSP:
		itnim = BFA_ITNIM_FROM_TAG(fcpim,
						msg.delete_rsp->bfa_handle);
		WARN_ON(msg.delete_rsp->status != BFA_STATUS_OK);
		bfa_stats(itnim, delete_comps);
		bfa_sm_send_event(itnim, BFA_ITNIM_SM_FWRSP);
		break;

	case BFI_ITN_I2H_SLER_EVENT:
		itnim = BFA_ITNIM_FROM_TAG(fcpim,
						msg.sler_event->bfa_handle);
		bfa_stats(itnim, sler_events);
		bfa_sm_send_event(itnim, BFA_ITNIM_SM_SLER);
		break;

	default:
		bfa_trc(bfa, m->mhdr.msg_id);
		WARN_ON(1);
	}
}

/*
 * bfa_itnim_api
 */

struct bfa_itnim_s *
bfa_itnim_create(struct bfa_s *bfa, struct bfa_rport_s *rport, void *ditn)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	struct bfa_itnim_s *itnim;

	bfa_itn_create(bfa, rport, bfa_itnim_isr);

	itnim = BFA_ITNIM_FROM_TAG(fcpim, rport->rport_tag);
	WARN_ON(itnim->rport != rport);

	itnim->ditn = ditn;

	bfa_stats(itnim, creates);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_CREATE);

	return itnim;
}

void
bfa_itnim_delete(struct bfa_itnim_s *itnim)
{
	bfa_stats(itnim, deletes);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_DELETE);
}

void
bfa_itnim_online(struct bfa_itnim_s *itnim, bfa_boolean_t seq_rec)
{
	itnim->seq_rec = seq_rec;
	bfa_stats(itnim, onlines);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_ONLINE);
}

void
bfa_itnim_offline(struct bfa_itnim_s *itnim)
{
	bfa_stats(itnim, offlines);
	bfa_sm_send_event(itnim, BFA_ITNIM_SM_OFFLINE);
}

/*
 * Return true if itnim is considered offline for holding off IO request.
 * IO is not held if itnim is being deleted.
 */
bfa_boolean_t
bfa_itnim_hold_io(struct bfa_itnim_s *itnim)
{
	return itnim->fcpim->path_tov && itnim->iotov_active &&
		(bfa_sm_cmp_state(itnim, bfa_itnim_sm_fwcreate) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_sler) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_cleanup_offline) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_fwdelete) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_offline) ||
		 bfa_sm_cmp_state(itnim, bfa_itnim_sm_iocdisable));
}

#define bfa_io_lat_clock_res_div	HZ
#define bfa_io_lat_clock_res_mul	1000
bfa_status_t
bfa_itnim_get_ioprofile(struct bfa_itnim_s *itnim,
			struct bfa_itnim_ioprofile_s *ioprofile)
{
	struct bfa_fcpim_s *fcpim;

	if (!itnim)
		return BFA_STATUS_NO_FCPIM_NEXUS;

	fcpim = BFA_FCPIM(itnim->bfa);

	if (!fcpim->io_profile)
		return BFA_STATUS_IOPROFILE_OFF;

	itnim->ioprofile.index = BFA_IOBUCKET_MAX;
	/* unsigned 32-bit time_t overflow here in y2106 */
	itnim->ioprofile.io_profile_start_time =
				bfa_io_profile_start_time(itnim->bfa);
	itnim->ioprofile.clock_res_mul = bfa_io_lat_clock_res_mul;
	itnim->ioprofile.clock_res_div = bfa_io_lat_clock_res_div;
	*ioprofile = itnim->ioprofile;

	return BFA_STATUS_OK;
}

void
bfa_itnim_clear_stats(struct bfa_itnim_s *itnim)
{
	int j;

	if (!itnim)
		return;

	memset(&itnim->stats, 0, sizeof(itnim->stats));
	memset(&itnim->ioprofile, 0, sizeof(itnim->ioprofile));
	for (j = 0; j < BFA_IOBUCKET_MAX; j++)
		itnim->ioprofile.io_latency.min[j] = ~0;
}

/*
 *  BFA IO module state machine functions
 */

/*
 * IO is not started (unallocated).
 */
static void
bfa_ioim_sm_uninit(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	switch (event) {
	case BFA_IOIM_SM_START:
		if (!bfa_itnim_is_online(ioim->itnim)) {
			if (!bfa_itnim_hold_io(ioim->itnim)) {
				bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
				list_del(&ioim->qe);
				list_add_tail(&ioim->qe,
					&ioim->fcpim->ioim_comp_q);
				bfa_cb_queue(ioim->bfa, &ioim->hcb_qe,
						__bfa_cb_ioim_pathtov, ioim);
			} else {
				list_del(&ioim->qe);
				list_add_tail(&ioim->qe,
					&ioim->itnim->pending_q);
			}
			break;
		}

		if (ioim->nsges > BFI_SGE_INLINE) {
			if (!bfa_ioim_sgpg_alloc(ioim)) {
				bfa_sm_set_state(ioim, bfa_ioim_sm_sgalloc);
				return;
			}
		}

		if (!bfa_ioim_send_ioreq(ioim)) {
			bfa_sm_set_state(ioim, bfa_ioim_sm_qfull);
			break;
		}

		bfa_sm_set_state(ioim, bfa_ioim_sm_active);
		break;

	case BFA_IOIM_SM_IOTOV:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe,
				__bfa_cb_ioim_pathtov, ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		/*
		 * IO in pending queue can get abort requests. Complete abort
		 * requests immediately.
		 */
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		WARN_ON(!bfa_q_is_on_q(&ioim->itnim->pending_q, ioim));
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe,
			__bfa_cb_ioim_abort, ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * IO is waiting for SG pages.
 */
static void
bfa_ioim_sm_sgalloc(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_SGALLOCED:
		if (!bfa_ioim_send_ioreq(ioim)) {
			bfa_sm_set_state(ioim, bfa_ioim_sm_qfull);
			break;
		}
		bfa_sm_set_state(ioim, bfa_ioim_sm_active);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_sgpg_wcancel(ioim->bfa, &ioim->iosp->sgpg_wqe);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_sgpg_wcancel(ioim->bfa, &ioim->iosp->sgpg_wqe);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_sgpg_wcancel(ioim->bfa, &ioim->iosp->sgpg_wqe);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * IO is active.
 */
static void
bfa_ioim_sm_active(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	switch (event) {
	case BFA_IOIM_SM_COMP_GOOD:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe,
			      __bfa_cb_ioim_good_comp, ioim);
		break;

	case BFA_IOIM_SM_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_comp,
			      ioim);
		break;

	case BFA_IOIM_SM_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_comp,
			      ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		ioim->iosp->abort_explicit = BFA_TRUE;
		ioim->io_cbfn = __bfa_cb_ioim_abort;

		if (bfa_ioim_send_abort(ioim))
			bfa_sm_set_state(ioim, bfa_ioim_sm_abort);
		else {
			bfa_sm_set_state(ioim, bfa_ioim_sm_abort_qfull);
			bfa_stats(ioim->itnim, qwait);
			bfa_reqq_wait(ioim->bfa, ioim->reqq,
					  &ioim->iosp->reqq_wait);
		}
		break;

	case BFA_IOIM_SM_CLEANUP:
		ioim->iosp->abort_explicit = BFA_FALSE;
		ioim->io_cbfn = __bfa_cb_ioim_failed;

		if (bfa_ioim_send_abort(ioim))
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup);
		else {
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup_qfull);
			bfa_stats(ioim->itnim, qwait);
			bfa_reqq_wait(ioim->bfa, ioim->reqq,
					  &ioim->iosp->reqq_wait);
		}
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	case BFA_IOIM_SM_SQRETRY:
		if (bfa_ioim_maxretry_reached(ioim)) {
			/* max retry reached, free IO */
			bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
			bfa_ioim_move_to_comp_q(ioim);
			bfa_cb_queue(ioim->bfa, &ioim->hcb_qe,
					__bfa_cb_ioim_failed, ioim);
			break;
		}
		/* waiting for IO tag resource free */
		bfa_sm_set_state(ioim, bfa_ioim_sm_cmnd_retry);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * IO is retried with new tag.
 */
static void
bfa_ioim_sm_cmnd_retry(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	switch (event) {
	case BFA_IOIM_SM_FREE:
		/* abts and rrq done. Now retry the IO with new tag */
		bfa_ioim_update_iotag(ioim);
		if (!bfa_ioim_send_ioreq(ioim)) {
			bfa_sm_set_state(ioim, bfa_ioim_sm_qfull);
			break;
		}
		bfa_sm_set_state(ioim, bfa_ioim_sm_active);
	break;

	case BFA_IOIM_SM_CLEANUP:
		ioim->iosp->abort_explicit = BFA_FALSE;
		ioim->io_cbfn = __bfa_cb_ioim_failed;

		if (bfa_ioim_send_abort(ioim))
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup);
		else {
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup_qfull);
			bfa_stats(ioim->itnim, qwait);
			bfa_reqq_wait(ioim->bfa, ioim->reqq,
					  &ioim->iosp->reqq_wait);
		}
	break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe,
			 __bfa_cb_ioim_failed, ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		/* in this state IO abort is done.
		 * Waiting for IO tag resource free.
		 */
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * IO is being aborted, waiting for completion from firmware.
 */
static void
bfa_ioim_sm_abort(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_COMP_GOOD:
	case BFA_IOIM_SM_COMP:
	case BFA_IOIM_SM_DONE:
	case BFA_IOIM_SM_FREE:
		break;

	case BFA_IOIM_SM_ABORT_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_ABORT_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_COMP_UTAG:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		WARN_ON(ioim->iosp->abort_explicit != BFA_TRUE);
		ioim->iosp->abort_explicit = BFA_FALSE;

		if (bfa_ioim_send_abort(ioim))
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup);
		else {
			bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup_qfull);
			bfa_stats(ioim->itnim, qwait);
			bfa_reqq_wait(ioim->bfa, ioim->reqq,
					  &ioim->iosp->reqq_wait);
		}
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * IO is being cleaned up (implicit abort), waiting for completion from
 * firmware.
 */
static void
bfa_ioim_sm_cleanup(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_COMP_GOOD:
	case BFA_IOIM_SM_COMP:
	case BFA_IOIM_SM_DONE:
	case BFA_IOIM_SM_FREE:
		break;

	case BFA_IOIM_SM_ABORT:
		/*
		 * IO is already being aborted implicitly
		 */
		ioim->io_cbfn = __bfa_cb_ioim_abort;
		break;

	case BFA_IOIM_SM_ABORT_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_ABORT_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_COMP_UTAG:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		/*
		 * IO can be in cleanup state already due to TM command.
		 * 2nd cleanup request comes from ITN offline event.
		 */
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * IO is waiting for room in request CQ
 */
static void
bfa_ioim_sm_qfull(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_QRESUME:
		bfa_sm_set_state(ioim, bfa_ioim_sm_active);
		bfa_ioim_send_ioreq(ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * Active IO is being aborted, waiting for room in request CQ.
 */
static void
bfa_ioim_sm_abort_qfull(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_QRESUME:
		bfa_sm_set_state(ioim, bfa_ioim_sm_abort);
		bfa_ioim_send_abort(ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		WARN_ON(ioim->iosp->abort_explicit != BFA_TRUE);
		ioim->iosp->abort_explicit = BFA_FALSE;
		bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup_qfull);
		break;

	case BFA_IOIM_SM_COMP_GOOD:
	case BFA_IOIM_SM_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_abort,
			      ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * Active IO is being cleaned up, waiting for room in request CQ.
 */
static void
bfa_ioim_sm_cleanup_qfull(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_QRESUME:
		bfa_sm_set_state(ioim, bfa_ioim_sm_cleanup);
		bfa_ioim_send_abort(ioim);
		break;

	case BFA_IOIM_SM_ABORT:
		/*
		 * IO is already being cleaned up implicitly
		 */
		ioim->io_cbfn = __bfa_cb_ioim_abort;
		break;

	case BFA_IOIM_SM_COMP_GOOD:
	case BFA_IOIM_SM_COMP:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_DONE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb_free);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		bfa_reqq_wcancel(&ioim->iosp->reqq_wait);
		bfa_ioim_move_to_comp_q(ioim);
		bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, __bfa_cb_ioim_failed,
			      ioim);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * IO bfa callback is pending.
 */
static void
bfa_ioim_sm_hcb(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	switch (event) {
	case BFA_IOIM_SM_HCB:
		bfa_sm_set_state(ioim, bfa_ioim_sm_uninit);
		bfa_ioim_free(ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * IO bfa callback is pending. IO resource cannot be freed.
 */
static void
bfa_ioim_sm_hcb_free(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_HCB:
		bfa_sm_set_state(ioim, bfa_ioim_sm_resfree);
		list_del(&ioim->qe);
		list_add_tail(&ioim->qe, &ioim->fcpim->ioim_resfree_q);
		break;

	case BFA_IOIM_SM_FREE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		bfa_sm_set_state(ioim, bfa_ioim_sm_hcb);
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * IO is completed, waiting resource free from firmware.
 */
static void
bfa_ioim_sm_resfree(struct bfa_ioim_s *ioim, enum bfa_ioim_event event)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, event);

	switch (event) {
	case BFA_IOIM_SM_FREE:
		bfa_sm_set_state(ioim, bfa_ioim_sm_uninit);
		bfa_ioim_free(ioim);
		break;

	case BFA_IOIM_SM_CLEANUP:
		bfa_ioim_notify_cleanup(ioim);
		break;

	case BFA_IOIM_SM_HWFAIL:
		break;

	default:
		bfa_sm_fault(ioim->bfa, event);
	}
}

/*
 * This is called from bfa_fcpim_start after the bfa_init() with flash read
 * is complete by driver. now invalidate the stale content of lun mask
 * like unit attention, rp tag and lp tag.
 */
void
bfa_ioim_lm_init(struct bfa_s *bfa)
{
	struct bfa_lun_mask_s *lunm_list;
	int	i;

	if (bfa_get_lun_mask_status(bfa) == BFA_LUNMASK_MINCFG)
		return;

	lunm_list = bfa_get_lun_mask_list(bfa);
	for (i = 0; i < MAX_LUN_MASK_CFG; i++) {
		lunm_list[i].ua = BFA_IOIM_LM_UA_RESET;
		lunm_list[i].lp_tag = BFA_LP_TAG_INVALID;
		lunm_list[i].rp_tag = BFA_RPORT_TAG_INVALID;
	}
}

static void
__bfa_cb_ioim_good_comp(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s *ioim = cbarg;

	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	bfa_cb_ioim_good_comp(ioim->bfa->bfad, ioim->dio);
}

static void
__bfa_cb_ioim_comp(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s	*ioim = cbarg;
	struct bfi_ioim_rsp_s *m;
	u8	*snsinfo = NULL;
	u8	sns_len = 0;
	s32	residue = 0;

	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	m = (struct bfi_ioim_rsp_s *) &ioim->iosp->comp_rspmsg;
	if (m->io_status == BFI_IOIM_STS_OK) {
		/*
		 * setup sense information, if present
		 */
		if ((m->scsi_status == SCSI_STATUS_CHECK_CONDITION) &&
					m->sns_len) {
			sns_len = m->sns_len;
			snsinfo = BFA_SNSINFO_FROM_TAG(ioim->fcpim->fcp,
						ioim->iotag);
		}

		/*
		 * setup residue value correctly for normal completions
		 */
		if (m->resid_flags == FCP_RESID_UNDER) {
			residue = be32_to_cpu(m->residue);
			bfa_stats(ioim->itnim, iocomp_underrun);
		}
		if (m->resid_flags == FCP_RESID_OVER) {
			residue = be32_to_cpu(m->residue);
			residue = -residue;
			bfa_stats(ioim->itnim, iocomp_overrun);
		}
	}

	bfa_cb_ioim_done(ioim->bfa->bfad, ioim->dio, m->io_status,
			  m->scsi_status, sns_len, snsinfo, residue);
}

void
bfa_fcpim_lunmask_rp_update(struct bfa_s *bfa, wwn_t lp_wwn, wwn_t rp_wwn,
			u16 rp_tag, u8 lp_tag)
{
	struct bfa_lun_mask_s *lun_list;
	u8	i;

	if (bfa_get_lun_mask_status(bfa) == BFA_LUNMASK_MINCFG)
		return;

	lun_list = bfa_get_lun_mask_list(bfa);
	for (i = 0; i < MAX_LUN_MASK_CFG; i++) {
		if (lun_list[i].state == BFA_IOIM_LUN_MASK_ACTIVE) {
			if ((lun_list[i].lp_wwn == lp_wwn) &&
			    (lun_list[i].rp_wwn == rp_wwn)) {
				lun_list[i].rp_tag = rp_tag;
				lun_list[i].lp_tag = lp_tag;
			}
		}
	}
}

/*
 * set UA for all active luns in LM DB
 */
static void
bfa_ioim_lm_set_ua(struct bfa_s *bfa)
{
	struct bfa_lun_mask_s	*lunm_list;
	int	i;

	lunm_list = bfa_get_lun_mask_list(bfa);
	for (i = 0; i < MAX_LUN_MASK_CFG; i++) {
		if (lunm_list[i].state != BFA_IOIM_LUN_MASK_ACTIVE)
			continue;
		lunm_list[i].ua = BFA_IOIM_LM_UA_SET;
	}
}

bfa_status_t
bfa_fcpim_lunmask_update(struct bfa_s *bfa, u32 update)
{
	struct bfa_lunmask_cfg_s	*lun_mask;

	bfa_trc(bfa, bfa_get_lun_mask_status(bfa));
	if (bfa_get_lun_mask_status(bfa) == BFA_LUNMASK_MINCFG)
		return BFA_STATUS_FAILED;

	if (bfa_get_lun_mask_status(bfa) == update)
		return BFA_STATUS_NO_CHANGE;

	lun_mask = bfa_get_lun_mask(bfa);
	lun_mask->status = update;

	if (bfa_get_lun_mask_status(bfa) == BFA_LUNMASK_ENABLED)
		bfa_ioim_lm_set_ua(bfa);

	return  bfa_dconf_update(bfa);
}

bfa_status_t
bfa_fcpim_lunmask_clear(struct bfa_s *bfa)
{
	int i;
	struct bfa_lun_mask_s	*lunm_list;

	bfa_trc(bfa, bfa_get_lun_mask_status(bfa));
	if (bfa_get_lun_mask_status(bfa) == BFA_LUNMASK_MINCFG)
		return BFA_STATUS_FAILED;

	lunm_list = bfa_get_lun_mask_list(bfa);
	for (i = 0; i < MAX_LUN_MASK_CFG; i++) {
		if (lunm_list[i].state == BFA_IOIM_LUN_MASK_ACTIVE) {
			if (lunm_list[i].rp_tag != BFA_RPORT_TAG_INVALID)
				bfa_rport_unset_lunmask(bfa,
				  BFA_RPORT_FROM_TAG(bfa, lunm_list[i].rp_tag));
		}
	}

	memset(lunm_list, 0, sizeof(struct bfa_lun_mask_s) * MAX_LUN_MASK_CFG);
	return bfa_dconf_update(bfa);
}

bfa_status_t
bfa_fcpim_lunmask_query(struct bfa_s *bfa, void *buf)
{
	struct bfa_lunmask_cfg_s *lun_mask;

	bfa_trc(bfa, bfa_get_lun_mask_status(bfa));
	if (bfa_get_lun_mask_status(bfa) == BFA_LUNMASK_MINCFG)
		return BFA_STATUS_FAILED;

	lun_mask = bfa_get_lun_mask(bfa);
	memcpy(buf, lun_mask, sizeof(struct bfa_lunmask_cfg_s));
	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcpim_lunmask_add(struct bfa_s *bfa, u16 vf_id, wwn_t *pwwn,
		      wwn_t rpwwn, struct scsi_lun lun)
{
	struct bfa_lun_mask_s *lunm_list;
	struct bfa_rport_s *rp = NULL;
	int i, free_index = MAX_LUN_MASK_CFG + 1;
	struct bfa_fcs_lport_s *port = NULL;
	struct bfa_fcs_rport_s *rp_fcs;

	bfa_trc(bfa, bfa_get_lun_mask_status(bfa));
	if (bfa_get_lun_mask_status(bfa) == BFA_LUNMASK_MINCFG)
		return BFA_STATUS_FAILED;

	port = bfa_fcs_lookup_port(&((struct bfad_s *)bfa->bfad)->bfa_fcs,
				   vf_id, *pwwn);
	if (port) {
		*pwwn = port->port_cfg.pwwn;
		rp_fcs = bfa_fcs_lport_get_rport_by_pwwn(port, rpwwn);
		if (rp_fcs)
			rp = rp_fcs->bfa_rport;
	}

	lunm_list = bfa_get_lun_mask_list(bfa);
	/* if entry exists */
	for (i = 0; i < MAX_LUN_MASK_CFG; i++) {
		if (lunm_list[i].state != BFA_IOIM_LUN_MASK_ACTIVE)
			free_index = i;
		if ((lunm_list[i].lp_wwn == *pwwn) &&
		    (lunm_list[i].rp_wwn == rpwwn) &&
		    (scsilun_to_int((struct scsi_lun *)&lunm_list[i].lun) ==
		     scsilun_to_int((struct scsi_lun *)&lun)))
			return  BFA_STATUS_ENTRY_EXISTS;
	}

	if (free_index > MAX_LUN_MASK_CFG)
		return BFA_STATUS_MAX_ENTRY_REACHED;

	if (rp) {
		lunm_list[free_index].lp_tag = bfa_lps_get_tag_from_pid(bfa,
						   rp->rport_info.local_pid);
		lunm_list[free_index].rp_tag = rp->rport_tag;
	} else {
		lunm_list[free_index].lp_tag = BFA_LP_TAG_INVALID;
		lunm_list[free_index].rp_tag = BFA_RPORT_TAG_INVALID;
	}

	lunm_list[free_index].lp_wwn = *pwwn;
	lunm_list[free_index].rp_wwn = rpwwn;
	lunm_list[free_index].lun = lun;
	lunm_list[free_index].state = BFA_IOIM_LUN_MASK_ACTIVE;

	/* set for all luns in this rp */
	for (i = 0; i < MAX_LUN_MASK_CFG; i++) {
		if ((lunm_list[i].lp_wwn == *pwwn) &&
		    (lunm_list[i].rp_wwn == rpwwn))
			lunm_list[i].ua = BFA_IOIM_LM_UA_SET;
	}

	return bfa_dconf_update(bfa);
}

bfa_status_t
bfa_fcpim_lunmask_delete(struct bfa_s *bfa, u16 vf_id, wwn_t *pwwn,
			 wwn_t rpwwn, struct scsi_lun lun)
{
	struct bfa_lun_mask_s	*lunm_list;
	struct bfa_rport_s	*rp = NULL;
	struct bfa_fcs_lport_s *port = NULL;
	struct bfa_fcs_rport_s *rp_fcs;
	int	i;

	/* in min cfg lunm_list could be NULL but  no commands should run. */
	if (bfa_get_lun_mask_status(bfa) == BFA_LUNMASK_MINCFG)
		return BFA_STATUS_FAILED;

	bfa_trc(bfa, bfa_get_lun_mask_status(bfa));
	bfa_trc(bfa, *pwwn);
	bfa_trc(bfa, rpwwn);
	bfa_trc(bfa, scsilun_to_int((struct scsi_lun *)&lun));

	if (*pwwn == 0) {
		port = bfa_fcs_lookup_port(
				&((struct bfad_s *)bfa->bfad)->bfa_fcs,
				vf_id, *pwwn);
		if (port) {
			*pwwn = port->port_cfg.pwwn;
			rp_fcs = bfa_fcs_lport_get_rport_by_pwwn(port, rpwwn);
			if (rp_fcs)
				rp = rp_fcs->bfa_rport;
		}
	}

	lunm_list = bfa_get_lun_mask_list(bfa);
	for (i = 0; i < MAX_LUN_MASK_CFG; i++) {
		if ((lunm_list[i].lp_wwn == *pwwn) &&
		    (lunm_list[i].rp_wwn == rpwwn) &&
		    (scsilun_to_int((struct scsi_lun *)&lunm_list[i].lun) ==
		     scsilun_to_int((struct scsi_lun *)&lun))) {
			lunm_list[i].lp_wwn = 0;
			lunm_list[i].rp_wwn = 0;
			int_to_scsilun(0, &lunm_list[i].lun);
			lunm_list[i].state = BFA_IOIM_LUN_MASK_INACTIVE;
			if (lunm_list[i].rp_tag != BFA_RPORT_TAG_INVALID) {
				lunm_list[i].rp_tag = BFA_RPORT_TAG_INVALID;
				lunm_list[i].lp_tag = BFA_LP_TAG_INVALID;
			}
			return bfa_dconf_update(bfa);
		}
	}

	/* set for all luns in this rp */
	for (i = 0; i < MAX_LUN_MASK_CFG; i++) {
		if ((lunm_list[i].lp_wwn == *pwwn) &&
		    (lunm_list[i].rp_wwn == rpwwn))
			lunm_list[i].ua = BFA_IOIM_LM_UA_SET;
	}

	return BFA_STATUS_ENTRY_NOT_EXISTS;
}

static void
__bfa_cb_ioim_failed(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s *ioim = cbarg;

	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	bfa_cb_ioim_done(ioim->bfa->bfad, ioim->dio, BFI_IOIM_STS_ABORTED,
			  0, 0, NULL, 0);
}

static void
__bfa_cb_ioim_pathtov(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s *ioim = cbarg;

	bfa_stats(ioim->itnim, path_tov_expired);
	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	bfa_cb_ioim_done(ioim->bfa->bfad, ioim->dio, BFI_IOIM_STS_PATHTOV,
			  0, 0, NULL, 0);
}

static void
__bfa_cb_ioim_abort(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_ioim_s *ioim = cbarg;

	if (!complete) {
		bfa_sm_send_event(ioim, BFA_IOIM_SM_HCB);
		return;
	}

	bfa_cb_ioim_abort(ioim->bfa->bfad, ioim->dio);
}

static void
bfa_ioim_sgpg_alloced(void *cbarg)
{
	struct bfa_ioim_s *ioim = cbarg;

	ioim->nsgpgs = BFA_SGPG_NPAGE(ioim->nsges);
	list_splice_tail_init(&ioim->iosp->sgpg_wqe.sgpg_q, &ioim->sgpg_q);
	ioim->sgpg = bfa_q_first(&ioim->sgpg_q);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_SGALLOCED);
}

/*
 * Send I/O request to firmware.
 */
static	bfa_boolean_t
bfa_ioim_send_ioreq(struct bfa_ioim_s *ioim)
{
	struct bfa_itnim_s *itnim = ioim->itnim;
	struct bfi_ioim_req_s *m;
	static struct fcp_cmnd_s cmnd_z0 = { { { 0 } } };
	struct bfi_sge_s *sge, *sgpge;
	u32	pgdlen = 0;
	u32	fcp_dl;
	u64 addr;
	struct scatterlist *sg;
	struct bfa_sgpg_s *sgpg;
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *) ioim->dio;
	u32 i, sge_id, pgcumsz;
	enum dma_data_direction dmadir;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(ioim->bfa, ioim->reqq);
	if (!m) {
		bfa_stats(ioim->itnim, qwait);
		bfa_reqq_wait(ioim->bfa, ioim->reqq,
				  &ioim->iosp->reqq_wait);
		return BFA_FALSE;
	}

	/*
	 * build i/o request message next
	 */
	m->io_tag = cpu_to_be16(ioim->iotag);
	m->rport_hdl = ioim->itnim->rport->fw_handle;
	m->io_timeout = 0;

	sge = &m->sges[0];
	sgpg = ioim->sgpg;
	sge_id = 0;
	sgpge = NULL;
	pgcumsz = 0;
	scsi_for_each_sg(cmnd, sg, ioim->nsges, i) {
		if (i == 0) {
			/* build inline IO SG element */
			addr = bfa_sgaddr_le(sg_dma_address(sg));
			sge->sga = *(union bfi_addr_u *) &addr;
			pgdlen = sg_dma_len(sg);
			sge->sg_len = pgdlen;
			sge->flags = (ioim->nsges > BFI_SGE_INLINE) ?
					BFI_SGE_DATA_CPL : BFI_SGE_DATA_LAST;
			bfa_sge_to_be(sge);
			sge++;
		} else {
			if (sge_id == 0)
				sgpge = sgpg->sgpg->sges;

			addr = bfa_sgaddr_le(sg_dma_address(sg));
			sgpge->sga = *(union bfi_addr_u *) &addr;
			sgpge->sg_len = sg_dma_len(sg);
			pgcumsz += sgpge->sg_len;

			/* set flags */
			if (i < (ioim->nsges - 1) &&
					sge_id < (BFI_SGPG_DATA_SGES - 1))
				sgpge->flags = BFI_SGE_DATA;
			else if (i < (ioim->nsges - 1))
				sgpge->flags = BFI_SGE_DATA_CPL;
			else
				sgpge->flags = BFI_SGE_DATA_LAST;

			bfa_sge_to_le(sgpge);

			sgpge++;
			if (i == (ioim->nsges - 1)) {
				sgpge->flags = BFI_SGE_PGDLEN;
				sgpge->sga.a32.addr_lo = 0;
				sgpge->sga.a32.addr_hi = 0;
				sgpge->sg_len = pgcumsz;
				bfa_sge_to_le(sgpge);
			} else if (++sge_id == BFI_SGPG_DATA_SGES) {
				sgpg = (struct bfa_sgpg_s *) bfa_q_next(sgpg);
				sgpge->flags = BFI_SGE_LINK;
				sgpge->sga = sgpg->sgpg_pa;
				sgpge->sg_len = pgcumsz;
				bfa_sge_to_le(sgpge);
				sge_id = 0;
				pgcumsz = 0;
			}
		}
	}

	if (ioim->nsges > BFI_SGE_INLINE) {
		sge->sga = ioim->sgpg->sgpg_pa;
	} else {
		sge->sga.a32.addr_lo = 0;
		sge->sga.a32.addr_hi = 0;
	}
	sge->sg_len = pgdlen;
	sge->flags = BFI_SGE_PGDLEN;
	bfa_sge_to_be(sge);

	/*
	 * set up I/O command parameters
	 */
	m->cmnd = cmnd_z0;
	int_to_scsilun(cmnd->device->lun, &m->cmnd.lun);
	dmadir = cmnd->sc_data_direction;
	if (dmadir == DMA_TO_DEVICE)
		m->cmnd.iodir = FCP_IODIR_WRITE;
	else if (dmadir == DMA_FROM_DEVICE)
		m->cmnd.iodir = FCP_IODIR_READ;
	else
		m->cmnd.iodir = FCP_IODIR_NONE;

	m->cmnd.cdb = *(struct scsi_cdb_s *) cmnd->cmnd;
	fcp_dl = scsi_bufflen(cmnd);
	m->cmnd.fcp_dl = cpu_to_be32(fcp_dl);

	/*
	 * set up I/O message header
	 */
	switch (m->cmnd.iodir) {
	case FCP_IODIR_READ:
		bfi_h2i_set(m->mh, BFI_MC_IOIM_READ, 0, bfa_fn_lpu(ioim->bfa));
		bfa_stats(itnim, input_reqs);
		ioim->itnim->stats.rd_throughput += fcp_dl;
		break;
	case FCP_IODIR_WRITE:
		bfi_h2i_set(m->mh, BFI_MC_IOIM_WRITE, 0, bfa_fn_lpu(ioim->bfa));
		bfa_stats(itnim, output_reqs);
		ioim->itnim->stats.wr_throughput += fcp_dl;
		break;
	case FCP_IODIR_RW:
		bfa_stats(itnim, input_reqs);
		bfa_stats(itnim, output_reqs);
		/* fall through */
	default:
		bfi_h2i_set(m->mh, BFI_MC_IOIM_IO, 0, bfa_fn_lpu(ioim->bfa));
	}
	if (itnim->seq_rec ||
	    (scsi_bufflen(cmnd) & (sizeof(u32) - 1)))
		bfi_h2i_set(m->mh, BFI_MC_IOIM_IO, 0, bfa_fn_lpu(ioim->bfa));

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(ioim->bfa, ioim->reqq, m->mh);
	return BFA_TRUE;
}

/*
 * Setup any additional SG pages needed.Inline SG element is setup
 * at queuing time.
 */
static bfa_boolean_t
bfa_ioim_sgpg_alloc(struct bfa_ioim_s *ioim)
{
	u16	nsgpgs;

	WARN_ON(ioim->nsges <= BFI_SGE_INLINE);

	/*
	 * allocate SG pages needed
	 */
	nsgpgs = BFA_SGPG_NPAGE(ioim->nsges);
	if (!nsgpgs)
		return BFA_TRUE;

	if (bfa_sgpg_malloc(ioim->bfa, &ioim->sgpg_q, nsgpgs)
	    != BFA_STATUS_OK) {
		bfa_sgpg_wait(ioim->bfa, &ioim->iosp->sgpg_wqe, nsgpgs);
		return BFA_FALSE;
	}

	ioim->nsgpgs = nsgpgs;
	ioim->sgpg = bfa_q_first(&ioim->sgpg_q);

	return BFA_TRUE;
}

/*
 * Send I/O abort request to firmware.
 */
static	bfa_boolean_t
bfa_ioim_send_abort(struct bfa_ioim_s *ioim)
{
	struct bfi_ioim_abort_req_s *m;
	enum bfi_ioim_h2i	msgop;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(ioim->bfa, ioim->reqq);
	if (!m)
		return BFA_FALSE;

	/*
	 * build i/o request message next
	 */
	if (ioim->iosp->abort_explicit)
		msgop = BFI_IOIM_H2I_IOABORT_REQ;
	else
		msgop = BFI_IOIM_H2I_IOCLEANUP_REQ;

	bfi_h2i_set(m->mh, BFI_MC_IOIM, msgop, bfa_fn_lpu(ioim->bfa));
	m->io_tag    = cpu_to_be16(ioim->iotag);
	m->abort_tag = ++ioim->abort_tag;

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(ioim->bfa, ioim->reqq, m->mh);
	return BFA_TRUE;
}

/*
 * Call to resume any I/O requests waiting for room in request queue.
 */
static void
bfa_ioim_qresume(void *cbarg)
{
	struct bfa_ioim_s *ioim = cbarg;

	bfa_stats(ioim->itnim, qresumes);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_QRESUME);
}


static void
bfa_ioim_notify_cleanup(struct bfa_ioim_s *ioim)
{
	/*
	 * Move IO from itnim queue to fcpim global queue since itnim will be
	 * freed.
	 */
	list_del(&ioim->qe);
	list_add_tail(&ioim->qe, &ioim->fcpim->ioim_comp_q);

	if (!ioim->iosp->tskim) {
		if (ioim->fcpim->delay_comp && ioim->itnim->iotov_active) {
			bfa_cb_dequeue(&ioim->hcb_qe);
			list_del(&ioim->qe);
			list_add_tail(&ioim->qe, &ioim->itnim->delay_comp_q);
		}
		bfa_itnim_iodone(ioim->itnim);
	} else
		bfa_wc_down(&ioim->iosp->tskim->wc);
}

static bfa_boolean_t
bfa_ioim_is_abortable(struct bfa_ioim_s *ioim)
{
	if ((bfa_sm_cmp_state(ioim, bfa_ioim_sm_uninit) &&
	    (!bfa_q_is_on_q(&ioim->itnim->pending_q, ioim)))	||
	    (bfa_sm_cmp_state(ioim, bfa_ioim_sm_abort))		||
	    (bfa_sm_cmp_state(ioim, bfa_ioim_sm_abort_qfull))	||
	    (bfa_sm_cmp_state(ioim, bfa_ioim_sm_hcb))		||
	    (bfa_sm_cmp_state(ioim, bfa_ioim_sm_hcb_free))	||
	    (bfa_sm_cmp_state(ioim, bfa_ioim_sm_resfree)))
		return BFA_FALSE;

	return BFA_TRUE;
}

void
bfa_ioim_delayed_comp(struct bfa_ioim_s *ioim, bfa_boolean_t iotov)
{
	/*
	 * If path tov timer expired, failback with PATHTOV status - these
	 * IO requests are not normally retried by IO stack.
	 *
	 * Otherwise device cameback online and fail it with normal failed
	 * status so that IO stack retries these failed IO requests.
	 */
	if (iotov)
		ioim->io_cbfn = __bfa_cb_ioim_pathtov;
	else {
		ioim->io_cbfn = __bfa_cb_ioim_failed;
		bfa_stats(ioim->itnim, iocom_nexus_abort);
	}
	bfa_cb_queue(ioim->bfa, &ioim->hcb_qe, ioim->io_cbfn, ioim);

	/*
	 * Move IO to fcpim global queue since itnim will be
	 * freed.
	 */
	list_del(&ioim->qe);
	list_add_tail(&ioim->qe, &ioim->fcpim->ioim_comp_q);
}


/*
 * Memory allocation and initialization.
 */
void
bfa_ioim_attach(struct bfa_fcpim_s *fcpim)
{
	struct bfa_ioim_s		*ioim;
	struct bfa_fcp_mod_s	*fcp = fcpim->fcp;
	struct bfa_ioim_sp_s	*iosp;
	u16		i;

	/*
	 * claim memory first
	 */
	ioim = (struct bfa_ioim_s *) bfa_mem_kva_curp(fcp);
	fcpim->ioim_arr = ioim;
	bfa_mem_kva_curp(fcp) = (u8 *) (ioim + fcpim->fcp->num_ioim_reqs);

	iosp = (struct bfa_ioim_sp_s *) bfa_mem_kva_curp(fcp);
	fcpim->ioim_sp_arr = iosp;
	bfa_mem_kva_curp(fcp) = (u8 *) (iosp + fcpim->fcp->num_ioim_reqs);

	/*
	 * Initialize ioim free queues
	 */
	INIT_LIST_HEAD(&fcpim->ioim_resfree_q);
	INIT_LIST_HEAD(&fcpim->ioim_comp_q);

	for (i = 0; i < fcpim->fcp->num_ioim_reqs;
	     i++, ioim++, iosp++) {
		/*
		 * initialize IOIM
		 */
		memset(ioim, 0, sizeof(struct bfa_ioim_s));
		ioim->iotag   = i;
		ioim->bfa     = fcpim->bfa;
		ioim->fcpim   = fcpim;
		ioim->iosp    = iosp;
		INIT_LIST_HEAD(&ioim->sgpg_q);
		bfa_reqq_winit(&ioim->iosp->reqq_wait,
				   bfa_ioim_qresume, ioim);
		bfa_sgpg_winit(&ioim->iosp->sgpg_wqe,
				   bfa_ioim_sgpg_alloced, ioim);
		bfa_sm_set_state(ioim, bfa_ioim_sm_uninit);
	}
}

void
bfa_ioim_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	struct bfi_ioim_rsp_s *rsp = (struct bfi_ioim_rsp_s *) m;
	struct bfa_ioim_s *ioim;
	u16	iotag;
	enum bfa_ioim_event evt = BFA_IOIM_SM_COMP;

	iotag = be16_to_cpu(rsp->io_tag);

	ioim = BFA_IOIM_FROM_TAG(fcpim, iotag);
	WARN_ON(ioim->iotag != iotag);

	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_trc(ioim->bfa, rsp->io_status);
	bfa_trc(ioim->bfa, rsp->reuse_io_tag);

	if (bfa_sm_cmp_state(ioim, bfa_ioim_sm_active))
		ioim->iosp->comp_rspmsg = *m;

	switch (rsp->io_status) {
	case BFI_IOIM_STS_OK:
		bfa_stats(ioim->itnim, iocomp_ok);
		if (rsp->reuse_io_tag == 0)
			evt = BFA_IOIM_SM_DONE;
		else
			evt = BFA_IOIM_SM_COMP;
		break;

	case BFI_IOIM_STS_TIMEDOUT:
		bfa_stats(ioim->itnim, iocomp_timedout);
		/* fall through */
	case BFI_IOIM_STS_ABORTED:
		rsp->io_status = BFI_IOIM_STS_ABORTED;
		bfa_stats(ioim->itnim, iocomp_aborted);
		if (rsp->reuse_io_tag == 0)
			evt = BFA_IOIM_SM_DONE;
		else
			evt = BFA_IOIM_SM_COMP;
		break;

	case BFI_IOIM_STS_PROTO_ERR:
		bfa_stats(ioim->itnim, iocom_proto_err);
		WARN_ON(!rsp->reuse_io_tag);
		evt = BFA_IOIM_SM_COMP;
		break;

	case BFI_IOIM_STS_SQER_NEEDED:
		bfa_stats(ioim->itnim, iocom_sqer_needed);
		WARN_ON(rsp->reuse_io_tag != 0);
		evt = BFA_IOIM_SM_SQRETRY;
		break;

	case BFI_IOIM_STS_RES_FREE:
		bfa_stats(ioim->itnim, iocom_res_free);
		evt = BFA_IOIM_SM_FREE;
		break;

	case BFI_IOIM_STS_HOST_ABORTED:
		bfa_stats(ioim->itnim, iocom_hostabrts);
		if (rsp->abort_tag != ioim->abort_tag) {
			bfa_trc(ioim->bfa, rsp->abort_tag);
			bfa_trc(ioim->bfa, ioim->abort_tag);
			return;
		}

		if (rsp->reuse_io_tag)
			evt = BFA_IOIM_SM_ABORT_COMP;
		else
			evt = BFA_IOIM_SM_ABORT_DONE;
		break;

	case BFI_IOIM_STS_UTAG:
		bfa_stats(ioim->itnim, iocom_utags);
		evt = BFA_IOIM_SM_COMP_UTAG;
		break;

	default:
		WARN_ON(1);
	}

	bfa_sm_send_event(ioim, evt);
}

void
bfa_ioim_good_comp_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	struct bfi_ioim_rsp_s *rsp = (struct bfi_ioim_rsp_s *) m;
	struct bfa_ioim_s *ioim;
	u16	iotag;

	iotag = be16_to_cpu(rsp->io_tag);

	ioim = BFA_IOIM_FROM_TAG(fcpim, iotag);
	WARN_ON(ioim->iotag != iotag);

	bfa_ioim_cb_profile_comp(fcpim, ioim);

	bfa_sm_send_event(ioim, BFA_IOIM_SM_COMP_GOOD);
}

/*
 * Called by itnim to clean up IO while going offline.
 */
void
bfa_ioim_cleanup(struct bfa_ioim_s *ioim)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_stats(ioim->itnim, io_cleanups);

	ioim->iosp->tskim = NULL;
	bfa_sm_send_event(ioim, BFA_IOIM_SM_CLEANUP);
}

void
bfa_ioim_cleanup_tm(struct bfa_ioim_s *ioim, struct bfa_tskim_s *tskim)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_stats(ioim->itnim, io_tmaborts);

	ioim->iosp->tskim = tskim;
	bfa_sm_send_event(ioim, BFA_IOIM_SM_CLEANUP);
}

/*
 * IOC failure handling.
 */
void
bfa_ioim_iocdisable(struct bfa_ioim_s *ioim)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_stats(ioim->itnim, io_iocdowns);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_HWFAIL);
}

/*
 * IO offline TOV popped. Fail the pending IO.
 */
void
bfa_ioim_tov(struct bfa_ioim_s *ioim)
{
	bfa_trc(ioim->bfa, ioim->iotag);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_IOTOV);
}


/*
 * Allocate IOIM resource for initiator mode I/O request.
 */
struct bfa_ioim_s *
bfa_ioim_alloc(struct bfa_s *bfa, struct bfad_ioim_s *dio,
		struct bfa_itnim_s *itnim, u16 nsges)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	struct bfa_ioim_s *ioim;
	struct bfa_iotag_s *iotag = NULL;

	/*
	 * alocate IOIM resource
	 */
	bfa_q_deq(&fcpim->fcp->iotag_ioim_free_q, &iotag);
	if (!iotag) {
		bfa_stats(itnim, no_iotags);
		return NULL;
	}

	ioim = BFA_IOIM_FROM_TAG(fcpim, iotag->tag);

	ioim->dio = dio;
	ioim->itnim = itnim;
	ioim->nsges = nsges;
	ioim->nsgpgs = 0;

	bfa_stats(itnim, total_ios);
	fcpim->ios_active++;

	list_add_tail(&ioim->qe, &itnim->io_q);

	return ioim;
}

void
bfa_ioim_free(struct bfa_ioim_s *ioim)
{
	struct bfa_fcpim_s *fcpim = ioim->fcpim;
	struct bfa_iotag_s *iotag;

	if (ioim->nsgpgs > 0)
		bfa_sgpg_mfree(ioim->bfa, &ioim->sgpg_q, ioim->nsgpgs);

	bfa_stats(ioim->itnim, io_comps);
	fcpim->ios_active--;

	ioim->iotag &= BFA_IOIM_IOTAG_MASK;

	WARN_ON(!(ioim->iotag <
		(fcpim->fcp->num_ioim_reqs + fcpim->fcp->num_fwtio_reqs)));
	iotag = BFA_IOTAG_FROM_TAG(fcpim->fcp, ioim->iotag);

	if (ioim->iotag < fcpim->fcp->num_ioim_reqs)
		list_add_tail(&iotag->qe, &fcpim->fcp->iotag_ioim_free_q);
	else
		list_add_tail(&iotag->qe, &fcpim->fcp->iotag_tio_free_q);

	list_del(&ioim->qe);
}

void
bfa_ioim_start(struct bfa_ioim_s *ioim)
{
	bfa_ioim_cb_profile_start(ioim->fcpim, ioim);

	/*
	 * Obtain the queue over which this request has to be issued
	 */
	ioim->reqq = bfa_fcpim_ioredirect_enabled(ioim->bfa) ?
			BFA_FALSE : bfa_itnim_get_reqq(ioim);

	bfa_sm_send_event(ioim, BFA_IOIM_SM_START);
}

/*
 * Driver I/O abort request.
 */
bfa_status_t
bfa_ioim_abort(struct bfa_ioim_s *ioim)
{

	bfa_trc(ioim->bfa, ioim->iotag);

	if (!bfa_ioim_is_abortable(ioim))
		return BFA_STATUS_FAILED;

	bfa_stats(ioim->itnim, io_aborts);
	bfa_sm_send_event(ioim, BFA_IOIM_SM_ABORT);

	return BFA_STATUS_OK;
}

/*
 *  BFA TSKIM state machine functions
 */

/*
 * Task management command beginning state.
 */
static void
bfa_tskim_sm_uninit(struct bfa_tskim_s *tskim, enum bfa_tskim_event event)
{
	bfa_trc(tskim->bfa, tskim->tsk_tag << 16 | event);

	switch (event) {
	case BFA_TSKIM_SM_START:
		bfa_sm_set_state(tskim, bfa_tskim_sm_active);
		bfa_tskim_gather_ios(tskim);

		/*
		 * If device is offline, do not send TM on wire. Just cleanup
		 * any pending IO requests and complete TM request.
		 */
		if (!bfa_itnim_is_online(tskim->itnim)) {
			bfa_sm_set_state(tskim, bfa_tskim_sm_iocleanup);
			tskim->tsk_status = BFI_TSKIM_STS_OK;
			bfa_tskim_cleanup_ios(tskim);
			return;
		}

		if (!bfa_tskim_send(tskim)) {
			bfa_sm_set_state(tskim, bfa_tskim_sm_qfull);
			bfa_stats(tskim->itnim, tm_qwait);
			bfa_reqq_wait(tskim->bfa, tskim->itnim->reqq,
					  &tskim->reqq_wait);
		}
		break;

	default:
		bfa_sm_fault(tskim->bfa, event);
	}
}

/*
 * TM command is active, awaiting completion from firmware to
 * cleanup IO requests in TM scope.
 */
static void
bfa_tskim_sm_active(struct bfa_tskim_s *tskim, enum bfa_tskim_event event)
{
	bfa_trc(tskim->bfa, tskim->tsk_tag << 16 | event);

	switch (event) {
	case BFA_TSKIM_SM_DONE:
		bfa_sm_set_state(tskim, bfa_tskim_sm_iocleanup);
		bfa_tskim_cleanup_ios(tskim);
		break;

	case BFA_TSKIM_SM_CLEANUP:
		bfa_sm_set_state(tskim, bfa_tskim_sm_cleanup);
		if (!bfa_tskim_send_abort(tskim)) {
			bfa_sm_set_state(tskim, bfa_tskim_sm_cleanup_qfull);
			bfa_stats(tskim->itnim, tm_qwait);
			bfa_reqq_wait(tskim->bfa, tskim->itnim->reqq,
				&tskim->reqq_wait);
		}
		break;

	case BFA_TSKIM_SM_HWFAIL:
		bfa_sm_set_state(tskim, bfa_tskim_sm_hcb);
		bfa_tskim_iocdisable_ios(tskim);
		bfa_tskim_qcomp(tskim, __bfa_cb_tskim_failed);
		break;

	default:
		bfa_sm_fault(tskim->bfa, event);
	}
}

/*
 * An active TM is being cleaned up since ITN is offline. Awaiting cleanup
 * completion event from firmware.
 */
static void
bfa_tskim_sm_cleanup(struct bfa_tskim_s *tskim, enum bfa_tskim_event event)
{
	bfa_trc(tskim->bfa, tskim->tsk_tag << 16 | event);

	switch (event) {
	case BFA_TSKIM_SM_DONE:
		/*
		 * Ignore and wait for ABORT completion from firmware.
		 */
		break;

	case BFA_TSKIM_SM_UTAG:
	case BFA_TSKIM_SM_CLEANUP_DONE:
		bfa_sm_set_state(tskim, bfa_tskim_sm_iocleanup);
		bfa_tskim_cleanup_ios(tskim);
		break;

	case BFA_TSKIM_SM_HWFAIL:
		bfa_sm_set_state(tskim, bfa_tskim_sm_hcb);
		bfa_tskim_iocdisable_ios(tskim);
		bfa_tskim_qcomp(tskim, __bfa_cb_tskim_failed);
		break;

	default:
		bfa_sm_fault(tskim->bfa, event);
	}
}

static void
bfa_tskim_sm_iocleanup(struct bfa_tskim_s *tskim, enum bfa_tskim_event event)
{
	bfa_trc(tskim->bfa, tskim->tsk_tag << 16 | event);

	switch (event) {
	case BFA_TSKIM_SM_IOS_DONE:
		bfa_sm_set_state(tskim, bfa_tskim_sm_hcb);
		bfa_tskim_qcomp(tskim, __bfa_cb_tskim_done);
		break;

	case BFA_TSKIM_SM_CLEANUP:
		/*
		 * Ignore, TM command completed on wire.
		 * Notify TM conmpletion on IO cleanup completion.
		 */
		break;

	case BFA_TSKIM_SM_HWFAIL:
		bfa_sm_set_state(tskim, bfa_tskim_sm_hcb);
		bfa_tskim_iocdisable_ios(tskim);
		bfa_tskim_qcomp(tskim, __bfa_cb_tskim_failed);
		break;

	default:
		bfa_sm_fault(tskim->bfa, event);
	}
}

/*
 * Task management command is waiting for room in request CQ
 */
static void
bfa_tskim_sm_qfull(struct bfa_tskim_s *tskim, enum bfa_tskim_event event)
{
	bfa_trc(tskim->bfa, tskim->tsk_tag << 16 | event);

	switch (event) {
	case BFA_TSKIM_SM_QRESUME:
		bfa_sm_set_state(tskim, bfa_tskim_sm_active);
		bfa_tskim_send(tskim);
		break;

	case BFA_TSKIM_SM_CLEANUP:
		/*
		 * No need to send TM on wire since ITN is offline.
		 */
		bfa_sm_set_state(tskim, bfa_tskim_sm_iocleanup);
		bfa_reqq_wcancel(&tskim->reqq_wait);
		bfa_tskim_cleanup_ios(tskim);
		break;

	case BFA_TSKIM_SM_HWFAIL:
		bfa_sm_set_state(tskim, bfa_tskim_sm_hcb);
		bfa_reqq_wcancel(&tskim->reqq_wait);
		bfa_tskim_iocdisable_ios(tskim);
		bfa_tskim_qcomp(tskim, __bfa_cb_tskim_failed);
		break;

	default:
		bfa_sm_fault(tskim->bfa, event);
	}
}

/*
 * Task management command is active, awaiting for room in request CQ
 * to send clean up request.
 */
static void
bfa_tskim_sm_cleanup_qfull(struct bfa_tskim_s *tskim,
		enum bfa_tskim_event event)
{
	bfa_trc(tskim->bfa, tskim->tsk_tag << 16 | event);

	switch (event) {
	case BFA_TSKIM_SM_DONE:
		bfa_reqq_wcancel(&tskim->reqq_wait);
		/* fall through */
	case BFA_TSKIM_SM_QRESUME:
		bfa_sm_set_state(tskim, bfa_tskim_sm_cleanup);
		bfa_tskim_send_abort(tskim);
		break;

	case BFA_TSKIM_SM_HWFAIL:
		bfa_sm_set_state(tskim, bfa_tskim_sm_hcb);
		bfa_reqq_wcancel(&tskim->reqq_wait);
		bfa_tskim_iocdisable_ios(tskim);
		bfa_tskim_qcomp(tskim, __bfa_cb_tskim_failed);
		break;

	default:
		bfa_sm_fault(tskim->bfa, event);
	}
}

/*
 * BFA callback is pending
 */
static void
bfa_tskim_sm_hcb(struct bfa_tskim_s *tskim, enum bfa_tskim_event event)
{
	bfa_trc(tskim->bfa, tskim->tsk_tag << 16 | event);

	switch (event) {
	case BFA_TSKIM_SM_HCB:
		bfa_sm_set_state(tskim, bfa_tskim_sm_uninit);
		bfa_tskim_free(tskim);
		break;

	case BFA_TSKIM_SM_CLEANUP:
		bfa_tskim_notify_comp(tskim);
		break;

	case BFA_TSKIM_SM_HWFAIL:
		break;

	default:
		bfa_sm_fault(tskim->bfa, event);
	}
}

static void
__bfa_cb_tskim_done(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_tskim_s *tskim = cbarg;

	if (!complete) {
		bfa_sm_send_event(tskim, BFA_TSKIM_SM_HCB);
		return;
	}

	bfa_stats(tskim->itnim, tm_success);
	bfa_cb_tskim_done(tskim->bfa->bfad, tskim->dtsk, tskim->tsk_status);
}

static void
__bfa_cb_tskim_failed(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_tskim_s *tskim = cbarg;

	if (!complete) {
		bfa_sm_send_event(tskim, BFA_TSKIM_SM_HCB);
		return;
	}

	bfa_stats(tskim->itnim, tm_failures);
	bfa_cb_tskim_done(tskim->bfa->bfad, tskim->dtsk,
				BFI_TSKIM_STS_FAILED);
}

static bfa_boolean_t
bfa_tskim_match_scope(struct bfa_tskim_s *tskim, struct scsi_lun lun)
{
	switch (tskim->tm_cmnd) {
	case FCP_TM_TARGET_RESET:
		return BFA_TRUE;

	case FCP_TM_ABORT_TASK_SET:
	case FCP_TM_CLEAR_TASK_SET:
	case FCP_TM_LUN_RESET:
	case FCP_TM_CLEAR_ACA:
		return !memcmp(&tskim->lun, &lun, sizeof(lun));

	default:
		WARN_ON(1);
	}

	return BFA_FALSE;
}

/*
 * Gather affected IO requests and task management commands.
 */
static void
bfa_tskim_gather_ios(struct bfa_tskim_s *tskim)
{
	struct bfa_itnim_s *itnim = tskim->itnim;
	struct bfa_ioim_s *ioim;
	struct list_head *qe, *qen;
	struct scsi_cmnd *cmnd;
	struct scsi_lun scsilun;

	INIT_LIST_HEAD(&tskim->io_q);

	/*
	 * Gather any active IO requests first.
	 */
	list_for_each_safe(qe, qen, &itnim->io_q) {
		ioim = (struct bfa_ioim_s *) qe;
		cmnd = (struct scsi_cmnd *) ioim->dio;
		int_to_scsilun(cmnd->device->lun, &scsilun);
		if (bfa_tskim_match_scope(tskim, scsilun)) {
			list_del(&ioim->qe);
			list_add_tail(&ioim->qe, &tskim->io_q);
		}
	}

	/*
	 * Failback any pending IO requests immediately.
	 */
	list_for_each_safe(qe, qen, &itnim->pending_q) {
		ioim = (struct bfa_ioim_s *) qe;
		cmnd = (struct scsi_cmnd *) ioim->dio;
		int_to_scsilun(cmnd->device->lun, &scsilun);
		if (bfa_tskim_match_scope(tskim, scsilun)) {
			list_del(&ioim->qe);
			list_add_tail(&ioim->qe, &ioim->fcpim->ioim_comp_q);
			bfa_ioim_tov(ioim);
		}
	}
}

/*
 * IO cleanup completion
 */
static void
bfa_tskim_cleanp_comp(void *tskim_cbarg)
{
	struct bfa_tskim_s *tskim = tskim_cbarg;

	bfa_stats(tskim->itnim, tm_io_comps);
	bfa_sm_send_event(tskim, BFA_TSKIM_SM_IOS_DONE);
}

/*
 * Gather affected IO requests and task management commands.
 */
static void
bfa_tskim_cleanup_ios(struct bfa_tskim_s *tskim)
{
	struct bfa_ioim_s *ioim;
	struct list_head	*qe, *qen;

	bfa_wc_init(&tskim->wc, bfa_tskim_cleanp_comp, tskim);

	list_for_each_safe(qe, qen, &tskim->io_q) {
		ioim = (struct bfa_ioim_s *) qe;
		bfa_wc_up(&tskim->wc);
		bfa_ioim_cleanup_tm(ioim, tskim);
	}

	bfa_wc_wait(&tskim->wc);
}

/*
 * Send task management request to firmware.
 */
static bfa_boolean_t
bfa_tskim_send(struct bfa_tskim_s *tskim)
{
	struct bfa_itnim_s *itnim = tskim->itnim;
	struct bfi_tskim_req_s *m;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(tskim->bfa, itnim->reqq);
	if (!m)
		return BFA_FALSE;

	/*
	 * build i/o request message next
	 */
	bfi_h2i_set(m->mh, BFI_MC_TSKIM, BFI_TSKIM_H2I_TM_REQ,
			bfa_fn_lpu(tskim->bfa));

	m->tsk_tag = cpu_to_be16(tskim->tsk_tag);
	m->itn_fhdl = tskim->itnim->rport->fw_handle;
	m->t_secs = tskim->tsecs;
	m->lun = tskim->lun;
	m->tm_flags = tskim->tm_cmnd;

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(tskim->bfa, itnim->reqq, m->mh);
	return BFA_TRUE;
}

/*
 * Send abort request to cleanup an active TM to firmware.
 */
static bfa_boolean_t
bfa_tskim_send_abort(struct bfa_tskim_s *tskim)
{
	struct bfa_itnim_s	*itnim = tskim->itnim;
	struct bfi_tskim_abortreq_s	*m;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(tskim->bfa, itnim->reqq);
	if (!m)
		return BFA_FALSE;

	/*
	 * build i/o request message next
	 */
	bfi_h2i_set(m->mh, BFI_MC_TSKIM, BFI_TSKIM_H2I_ABORT_REQ,
			bfa_fn_lpu(tskim->bfa));

	m->tsk_tag  = cpu_to_be16(tskim->tsk_tag);

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(tskim->bfa, itnim->reqq, m->mh);
	return BFA_TRUE;
}

/*
 * Call to resume task management cmnd waiting for room in request queue.
 */
static void
bfa_tskim_qresume(void *cbarg)
{
	struct bfa_tskim_s *tskim = cbarg;

	bfa_stats(tskim->itnim, tm_qresumes);
	bfa_sm_send_event(tskim, BFA_TSKIM_SM_QRESUME);
}

/*
 * Cleanup IOs associated with a task mangement command on IOC failures.
 */
static void
bfa_tskim_iocdisable_ios(struct bfa_tskim_s *tskim)
{
	struct bfa_ioim_s *ioim;
	struct list_head	*qe, *qen;

	list_for_each_safe(qe, qen, &tskim->io_q) {
		ioim = (struct bfa_ioim_s *) qe;
		bfa_ioim_iocdisable(ioim);
	}
}

/*
 * Notification on completions from related ioim.
 */
void
bfa_tskim_iodone(struct bfa_tskim_s *tskim)
{
	bfa_wc_down(&tskim->wc);
}

/*
 * Handle IOC h/w failure notification from itnim.
 */
void
bfa_tskim_iocdisable(struct bfa_tskim_s *tskim)
{
	tskim->notify = BFA_FALSE;
	bfa_stats(tskim->itnim, tm_iocdowns);
	bfa_sm_send_event(tskim, BFA_TSKIM_SM_HWFAIL);
}

/*
 * Cleanup TM command and associated IOs as part of ITNIM offline.
 */
void
bfa_tskim_cleanup(struct bfa_tskim_s *tskim)
{
	tskim->notify = BFA_TRUE;
	bfa_stats(tskim->itnim, tm_cleanups);
	bfa_sm_send_event(tskim, BFA_TSKIM_SM_CLEANUP);
}

/*
 * Memory allocation and initialization.
 */
void
bfa_tskim_attach(struct bfa_fcpim_s *fcpim)
{
	struct bfa_tskim_s *tskim;
	struct bfa_fcp_mod_s	*fcp = fcpim->fcp;
	u16	i;

	INIT_LIST_HEAD(&fcpim->tskim_free_q);
	INIT_LIST_HEAD(&fcpim->tskim_unused_q);

	tskim = (struct bfa_tskim_s *) bfa_mem_kva_curp(fcp);
	fcpim->tskim_arr = tskim;

	for (i = 0; i < fcpim->num_tskim_reqs; i++, tskim++) {
		/*
		 * initialize TSKIM
		 */
		memset(tskim, 0, sizeof(struct bfa_tskim_s));
		tskim->tsk_tag = i;
		tskim->bfa	= fcpim->bfa;
		tskim->fcpim	= fcpim;
		tskim->notify  = BFA_FALSE;
		bfa_reqq_winit(&tskim->reqq_wait, bfa_tskim_qresume,
					tskim);
		bfa_sm_set_state(tskim, bfa_tskim_sm_uninit);

		list_add_tail(&tskim->qe, &fcpim->tskim_free_q);
	}

	bfa_mem_kva_curp(fcp) = (u8 *) tskim;
}

void
bfa_tskim_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	struct bfi_tskim_rsp_s *rsp = (struct bfi_tskim_rsp_s *) m;
	struct bfa_tskim_s *tskim;
	u16	tsk_tag = be16_to_cpu(rsp->tsk_tag);

	tskim = BFA_TSKIM_FROM_TAG(fcpim, tsk_tag);
	WARN_ON(tskim->tsk_tag != tsk_tag);

	tskim->tsk_status = rsp->tsk_status;

	/*
	 * Firmware sends BFI_TSKIM_STS_ABORTED status for abort
	 * requests. All other statuses are for normal completions.
	 */
	if (rsp->tsk_status == BFI_TSKIM_STS_ABORTED) {
		bfa_stats(tskim->itnim, tm_cleanup_comps);
		bfa_sm_send_event(tskim, BFA_TSKIM_SM_CLEANUP_DONE);
	} else if (rsp->tsk_status == BFI_TSKIM_STS_UTAG) {
		bfa_sm_send_event(tskim, BFA_TSKIM_SM_UTAG);
	} else {
		bfa_stats(tskim->itnim, tm_fw_rsps);
		bfa_sm_send_event(tskim, BFA_TSKIM_SM_DONE);
	}
}


struct bfa_tskim_s *
bfa_tskim_alloc(struct bfa_s *bfa, struct bfad_tskim_s *dtsk)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	struct bfa_tskim_s *tskim;

	bfa_q_deq(&fcpim->tskim_free_q, &tskim);

	if (tskim)
		tskim->dtsk = dtsk;

	return tskim;
}

void
bfa_tskim_free(struct bfa_tskim_s *tskim)
{
	WARN_ON(!bfa_q_is_on_q_func(&tskim->itnim->tsk_q, &tskim->qe));
	list_del(&tskim->qe);
	list_add_tail(&tskim->qe, &tskim->fcpim->tskim_free_q);
}

/*
 * Start a task management command.
 *
 * @param[in]	tskim	BFA task management command instance
 * @param[in]	itnim	i-t nexus for the task management command
 * @param[in]	lun	lun, if applicable
 * @param[in]	tm_cmnd	Task management command code.
 * @param[in]	t_secs	Timeout in seconds
 *
 * @return None.
 */
void
bfa_tskim_start(struct bfa_tskim_s *tskim, struct bfa_itnim_s *itnim,
			struct scsi_lun lun,
			enum fcp_tm_cmnd tm_cmnd, u8 tsecs)
{
	tskim->itnim	= itnim;
	tskim->lun	= lun;
	tskim->tm_cmnd = tm_cmnd;
	tskim->tsecs	= tsecs;
	tskim->notify  = BFA_FALSE;
	bfa_stats(itnim, tm_cmnds);

	list_add_tail(&tskim->qe, &itnim->tsk_q);
	bfa_sm_send_event(tskim, BFA_TSKIM_SM_START);
}

void
bfa_tskim_res_recfg(struct bfa_s *bfa, u16 num_tskim_fw)
{
	struct bfa_fcpim_s	*fcpim = BFA_FCPIM(bfa);
	struct list_head	*qe;
	int	i;

	for (i = 0; i < (fcpim->num_tskim_reqs - num_tskim_fw); i++) {
		bfa_q_deq_tail(&fcpim->tskim_free_q, &qe);
		list_add_tail(qe, &fcpim->tskim_unused_q);
	}
}

void
bfa_fcp_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *minfo,
		struct bfa_s *bfa)
{
	struct bfa_fcp_mod_s *fcp = BFA_FCP_MOD(bfa);
	struct bfa_mem_kva_s *fcp_kva = BFA_MEM_FCP_KVA(bfa);
	struct bfa_mem_dma_s *seg_ptr;
	u16	nsegs, idx, per_seg_ios, num_io_req;
	u32	km_len = 0;

	/*
	 * ZERO for num_ioim_reqs and num_fwtio_reqs is allowed config value.
	 * So if the values are non zero, adjust them appropriately.
	 */
	if (cfg->fwcfg.num_ioim_reqs &&
	    cfg->fwcfg.num_ioim_reqs < BFA_IOIM_MIN)
		cfg->fwcfg.num_ioim_reqs = BFA_IOIM_MIN;
	else if (cfg->fwcfg.num_ioim_reqs > BFA_IOIM_MAX)
		cfg->fwcfg.num_ioim_reqs = BFA_IOIM_MAX;

	if (cfg->fwcfg.num_fwtio_reqs > BFA_FWTIO_MAX)
		cfg->fwcfg.num_fwtio_reqs = BFA_FWTIO_MAX;

	num_io_req = (cfg->fwcfg.num_ioim_reqs + cfg->fwcfg.num_fwtio_reqs);
	if (num_io_req > BFA_IO_MAX) {
		if (cfg->fwcfg.num_ioim_reqs && cfg->fwcfg.num_fwtio_reqs) {
			cfg->fwcfg.num_ioim_reqs = BFA_IO_MAX/2;
			cfg->fwcfg.num_fwtio_reqs = BFA_IO_MAX/2;
		} else if (cfg->fwcfg.num_fwtio_reqs)
			cfg->fwcfg.num_fwtio_reqs = BFA_FWTIO_MAX;
		else
			cfg->fwcfg.num_ioim_reqs = BFA_IOIM_MAX;
	}

	bfa_fcpim_meminfo(cfg, &km_len);

	num_io_req = (cfg->fwcfg.num_ioim_reqs + cfg->fwcfg.num_fwtio_reqs);
	km_len += num_io_req * sizeof(struct bfa_iotag_s);
	km_len += cfg->fwcfg.num_rports * sizeof(struct bfa_itn_s);

	/* dma memory */
	nsegs = BFI_MEM_DMA_NSEGS(num_io_req, BFI_IOIM_SNSLEN);
	per_seg_ios = BFI_MEM_NREQS_SEG(BFI_IOIM_SNSLEN);

	bfa_mem_dma_seg_iter(fcp, seg_ptr, nsegs, idx) {
		if (num_io_req >= per_seg_ios) {
			num_io_req -= per_seg_ios;
			bfa_mem_dma_setup(minfo, seg_ptr,
				per_seg_ios * BFI_IOIM_SNSLEN);
		} else
			bfa_mem_dma_setup(minfo, seg_ptr,
				num_io_req * BFI_IOIM_SNSLEN);
	}

	/* kva memory */
	bfa_mem_kva_setup(minfo, fcp_kva, km_len);
}

void
bfa_fcp_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_pcidev_s *pcidev)
{
	struct bfa_fcp_mod_s *fcp = BFA_FCP_MOD(bfa);
	struct bfa_mem_dma_s *seg_ptr;
	u16	idx, nsegs, num_io_req;

	fcp->max_ioim_reqs = cfg->fwcfg.num_ioim_reqs;
	fcp->num_ioim_reqs = cfg->fwcfg.num_ioim_reqs;
	fcp->num_fwtio_reqs  = cfg->fwcfg.num_fwtio_reqs;
	fcp->num_itns   = cfg->fwcfg.num_rports;
	fcp->bfa = bfa;

	/*
	 * Setup the pool of snsbase addr's, that is passed to fw as
	 * part of bfi_iocfc_cfg_s.
	 */
	num_io_req = (cfg->fwcfg.num_ioim_reqs + cfg->fwcfg.num_fwtio_reqs);
	nsegs = BFI_MEM_DMA_NSEGS(num_io_req, BFI_IOIM_SNSLEN);

	bfa_mem_dma_seg_iter(fcp, seg_ptr, nsegs, idx) {

		if (!bfa_mem_dma_virt(seg_ptr))
			break;

		fcp->snsbase[idx].pa = bfa_mem_dma_phys(seg_ptr);
		fcp->snsbase[idx].kva = bfa_mem_dma_virt(seg_ptr);
		bfa_iocfc_set_snsbase(bfa, idx, fcp->snsbase[idx].pa);
	}

	fcp->throttle_update_required = 1;
	bfa_fcpim_attach(fcp, bfad, cfg, pcidev);

	bfa_iotag_attach(fcp);

	fcp->itn_arr = (struct bfa_itn_s *) bfa_mem_kva_curp(fcp);
	bfa_mem_kva_curp(fcp) = (u8 *)fcp->itn_arr +
			(fcp->num_itns * sizeof(struct bfa_itn_s));
	memset(fcp->itn_arr, 0,
			(fcp->num_itns * sizeof(struct bfa_itn_s)));
}

void
bfa_fcp_iocdisable(struct bfa_s *bfa)
{
	struct bfa_fcp_mod_s *fcp = BFA_FCP_MOD(bfa);

	bfa_fcpim_iocdisable(fcp);
}

void
bfa_fcp_res_recfg(struct bfa_s *bfa, u16 num_ioim_fw, u16 max_ioim_fw)
{
	struct bfa_fcp_mod_s	*mod = BFA_FCP_MOD(bfa);
	struct list_head	*qe;
	int	i;

	/* Update io throttle value only once during driver load time */
	if (!mod->throttle_update_required)
		return;

	for (i = 0; i < (mod->num_ioim_reqs - num_ioim_fw); i++) {
		bfa_q_deq_tail(&mod->iotag_ioim_free_q, &qe);
		list_add_tail(qe, &mod->iotag_unused_q);
	}

	if (mod->num_ioim_reqs != num_ioim_fw) {
		bfa_trc(bfa, mod->num_ioim_reqs);
		bfa_trc(bfa, num_ioim_fw);
	}

	mod->max_ioim_reqs = max_ioim_fw;
	mod->num_ioim_reqs = num_ioim_fw;
	mod->throttle_update_required = 0;
}

void
bfa_itn_create(struct bfa_s *bfa, struct bfa_rport_s *rport,
		void (*isr)(struct bfa_s *bfa, struct bfi_msg_s *m))
{
	struct bfa_fcp_mod_s *fcp = BFA_FCP_MOD(bfa);
	struct bfa_itn_s *itn;

	itn =  BFA_ITN_FROM_TAG(fcp, rport->rport_tag);
	itn->isr = isr;
}

/*
 * Itn interrupt processing.
 */
void
bfa_itn_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	struct bfa_fcp_mod_s *fcp = BFA_FCP_MOD(bfa);
	union bfi_itn_i2h_msg_u msg;
	struct bfa_itn_s *itn;

	msg.msg = m;
	itn =  BFA_ITN_FROM_TAG(fcp, msg.create_rsp->bfa_handle);

	if (itn->isr)
		itn->isr(bfa, m);
	else
		WARN_ON(1);
}

void
bfa_iotag_attach(struct bfa_fcp_mod_s *fcp)
{
	struct bfa_iotag_s *iotag;
	u16	num_io_req, i;

	iotag = (struct bfa_iotag_s *) bfa_mem_kva_curp(fcp);
	fcp->iotag_arr = iotag;

	INIT_LIST_HEAD(&fcp->iotag_ioim_free_q);
	INIT_LIST_HEAD(&fcp->iotag_tio_free_q);
	INIT_LIST_HEAD(&fcp->iotag_unused_q);

	num_io_req = fcp->num_ioim_reqs + fcp->num_fwtio_reqs;
	for (i = 0; i < num_io_req; i++, iotag++) {
		memset(iotag, 0, sizeof(struct bfa_iotag_s));
		iotag->tag = i;
		if (i < fcp->num_ioim_reqs)
			list_add_tail(&iotag->qe, &fcp->iotag_ioim_free_q);
		else
			list_add_tail(&iotag->qe, &fcp->iotag_tio_free_q);
	}

	bfa_mem_kva_curp(fcp) = (u8 *) iotag;
}


/**
 * To send config req, first try to use throttle value from flash
 * If 0, then use driver parameter
 * We need to use min(flash_val, drv_val) because
 * memory allocation was done based on this cfg'd value
 */
u16
bfa_fcpim_get_throttle_cfg(struct bfa_s *bfa, u16 drv_cfg_param)
{
	u16 tmp;
	struct bfa_fcp_mod_s *fcp = BFA_FCP_MOD(bfa);

	/*
	 * If throttle value from flash is already in effect after driver is
	 * loaded then until next load, always return current value instead
	 * of actual flash value
	 */
	if (!fcp->throttle_update_required)
		return (u16)fcp->num_ioim_reqs;

	tmp = bfa_dconf_read_data_valid(bfa) ? bfa_fcpim_read_throttle(bfa) : 0;
	if (!tmp || (tmp > drv_cfg_param))
		tmp = drv_cfg_param;

	return tmp;
}

bfa_status_t
bfa_fcpim_write_throttle(struct bfa_s *bfa, u16 value)
{
	if (!bfa_dconf_get_min_cfg(bfa)) {
		BFA_DCONF_MOD(bfa)->dconf->throttle_cfg.value = value;
		BFA_DCONF_MOD(bfa)->dconf->throttle_cfg.is_valid = 1;
		return BFA_STATUS_OK;
	}

	return BFA_STATUS_FAILED;
}

u16
bfa_fcpim_read_throttle(struct bfa_s *bfa)
{
	struct bfa_throttle_cfg_s *throttle_cfg =
			&(BFA_DCONF_MOD(bfa)->dconf->throttle_cfg);

	return ((!bfa_dconf_get_min_cfg(bfa)) ?
	       ((throttle_cfg->is_valid == 1) ? (throttle_cfg->value) : 0) : 0);
}

bfa_status_t
bfa_fcpim_throttle_set(struct bfa_s *bfa, u16 value)
{
	/* in min cfg no commands should run. */
	if ((bfa_dconf_get_min_cfg(bfa) == BFA_TRUE) ||
	    (!bfa_dconf_read_data_valid(bfa)))
		return BFA_STATUS_FAILED;

	bfa_fcpim_write_throttle(bfa, value);

	return bfa_dconf_update(bfa);
}

bfa_status_t
bfa_fcpim_throttle_get(struct bfa_s *bfa, void *buf)
{
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(bfa);
	struct bfa_defs_fcpim_throttle_s throttle;

	if ((bfa_dconf_get_min_cfg(bfa) == BFA_TRUE) ||
	    (!bfa_dconf_read_data_valid(bfa)))
		return BFA_STATUS_FAILED;

	memset(&throttle, 0, sizeof(struct bfa_defs_fcpim_throttle_s));

	throttle.cur_value = (u16)(fcpim->fcp->num_ioim_reqs);
	throttle.cfg_value = bfa_fcpim_read_throttle(bfa);
	if (!throttle.cfg_value)
		throttle.cfg_value = throttle.cur_value;
	throttle.max_value = (u16)(fcpim->fcp->max_ioim_reqs);
	memcpy(buf, &throttle, sizeof(struct bfa_defs_fcpim_throttle_s));

	return BFA_STATUS_OK;
}
