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
#include "bfad_im.h"
#include "bfa_plog.h"
#include "bfa_cs.h"
#include "bfa_modules.h"

BFA_TRC_FILE(HAL, FCXP);

/*
 * LPS related definitions
 */
#define BFA_LPS_MIN_LPORTS      (1)
#define BFA_LPS_MAX_LPORTS      (256)

/*
 * Maximum Vports supported per physical port or vf.
 */
#define BFA_LPS_MAX_VPORTS_SUPP_CB  255
#define BFA_LPS_MAX_VPORTS_SUPP_CT  190


/*
 * FC PORT related definitions
 */
/*
 * The port is considered disabled if corresponding physical port or IOC are
 * disabled explicitly
 */
#define BFA_PORT_IS_DISABLED(bfa) \
	((bfa_fcport_is_disabled(bfa) == BFA_TRUE) || \
	(bfa_ioc_is_disabled(&bfa->ioc) == BFA_TRUE))

/*
 * BFA port state machine events
 */
enum bfa_fcport_sm_event {
	BFA_FCPORT_SM_START	= 1,	/*  start port state machine	*/
	BFA_FCPORT_SM_STOP	= 2,	/*  stop port state machine	*/
	BFA_FCPORT_SM_ENABLE	= 3,	/*  enable port		*/
	BFA_FCPORT_SM_DISABLE	= 4,	/*  disable port state machine */
	BFA_FCPORT_SM_FWRSP	= 5,	/*  firmware enable/disable rsp */
	BFA_FCPORT_SM_LINKUP	= 6,	/*  firmware linkup event	*/
	BFA_FCPORT_SM_LINKDOWN	= 7,	/*  firmware linkup down	*/
	BFA_FCPORT_SM_QRESUME	= 8,	/*  CQ space available	*/
	BFA_FCPORT_SM_HWFAIL	= 9,	/*  IOC h/w failure		*/
	BFA_FCPORT_SM_DPORTENABLE = 10, /*  enable dport      */
	BFA_FCPORT_SM_DPORTDISABLE = 11,/*  disable dport     */
	BFA_FCPORT_SM_FAA_MISCONFIG = 12,	/* FAA misconfiguratin */
	BFA_FCPORT_SM_DDPORTENABLE  = 13,	/* enable ddport	*/
	BFA_FCPORT_SM_DDPORTDISABLE = 14,	/* disable ddport	*/
};

/*
 * BFA port link notification state machine events
 */

enum bfa_fcport_ln_sm_event {
	BFA_FCPORT_LN_SM_LINKUP		= 1,	/*  linkup event	*/
	BFA_FCPORT_LN_SM_LINKDOWN	= 2,	/*  linkdown event	*/
	BFA_FCPORT_LN_SM_NOTIFICATION	= 3	/*  done notification	*/
};

/*
 * RPORT related definitions
 */
#define bfa_rport_offline_cb(__rp) do {					\
	if ((__rp)->bfa->fcs)						\
		bfa_cb_rport_offline((__rp)->rport_drv);      \
	else {								\
		bfa_cb_queue((__rp)->bfa, &(__rp)->hcb_qe,		\
				__bfa_cb_rport_offline, (__rp));      \
	}								\
} while (0)

#define bfa_rport_online_cb(__rp) do {					\
	if ((__rp)->bfa->fcs)						\
		bfa_cb_rport_online((__rp)->rport_drv);      \
	else {								\
		bfa_cb_queue((__rp)->bfa, &(__rp)->hcb_qe,		\
				  __bfa_cb_rport_online, (__rp));      \
		}							\
} while (0)

/*
 * forward declarations FCXP related functions
 */
static void	__bfa_fcxp_send_cbfn(void *cbarg, bfa_boolean_t complete);
static void	hal_fcxp_rx_plog(struct bfa_s *bfa, struct bfa_fcxp_s *fcxp,
				struct bfi_fcxp_send_rsp_s *fcxp_rsp);
static void	hal_fcxp_tx_plog(struct bfa_s *bfa, u32 reqlen,
				struct bfa_fcxp_s *fcxp, struct fchs_s *fchs);
static void	bfa_fcxp_qresume(void *cbarg);
static void	bfa_fcxp_queue(struct bfa_fcxp_s *fcxp,
				struct bfi_fcxp_send_req_s *send_req);

/*
 * forward declarations for LPS functions
 */
static void bfa_lps_login_rsp(struct bfa_s *bfa,
				struct bfi_lps_login_rsp_s *rsp);
static void bfa_lps_no_res(struct bfa_lps_s *first_lps, u8 count);
static void bfa_lps_logout_rsp(struct bfa_s *bfa,
				struct bfi_lps_logout_rsp_s *rsp);
static void bfa_lps_reqq_resume(void *lps_arg);
static void bfa_lps_free(struct bfa_lps_s *lps);
static void bfa_lps_send_login(struct bfa_lps_s *lps);
static void bfa_lps_send_logout(struct bfa_lps_s *lps);
static void bfa_lps_send_set_n2n_pid(struct bfa_lps_s *lps);
static void bfa_lps_login_comp(struct bfa_lps_s *lps);
static void bfa_lps_logout_comp(struct bfa_lps_s *lps);
static void bfa_lps_cvl_event(struct bfa_lps_s *lps);

/*
 * forward declaration for LPS state machine
 */
static void bfa_lps_sm_init(struct bfa_lps_s *lps, enum bfa_lps_event event);
static void bfa_lps_sm_login(struct bfa_lps_s *lps, enum bfa_lps_event event);
static void bfa_lps_sm_loginwait(struct bfa_lps_s *lps, enum bfa_lps_event
					event);
static void bfa_lps_sm_online(struct bfa_lps_s *lps, enum bfa_lps_event event);
static void bfa_lps_sm_online_n2n_pid_wait(struct bfa_lps_s *lps,
					enum bfa_lps_event event);
static void bfa_lps_sm_logout(struct bfa_lps_s *lps, enum bfa_lps_event event);
static void bfa_lps_sm_logowait(struct bfa_lps_s *lps, enum bfa_lps_event
					event);

/*
 * forward declaration for FC Port functions
 */
static bfa_boolean_t bfa_fcport_send_enable(struct bfa_fcport_s *fcport);
static bfa_boolean_t bfa_fcport_send_disable(struct bfa_fcport_s *fcport);
static void bfa_fcport_update_linkinfo(struct bfa_fcport_s *fcport);
static void bfa_fcport_reset_linkinfo(struct bfa_fcport_s *fcport);
static void bfa_fcport_set_wwns(struct bfa_fcport_s *fcport);
static void __bfa_cb_fcport_event(void *cbarg, bfa_boolean_t complete);
static void bfa_fcport_scn(struct bfa_fcport_s *fcport,
			enum bfa_port_linkstate event, bfa_boolean_t trunk);
static void bfa_fcport_queue_cb(struct bfa_fcport_ln_s *ln,
				enum bfa_port_linkstate event);
static void __bfa_cb_fcport_stats_clr(void *cbarg, bfa_boolean_t complete);
static void bfa_fcport_stats_get_timeout(void *cbarg);
static void bfa_fcport_stats_clr_timeout(void *cbarg);
static void bfa_trunk_iocdisable(struct bfa_s *bfa);

/*
 * forward declaration for FC PORT state machine
 */
static void     bfa_fcport_sm_uninit(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_enabling_qwait(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_enabling(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_linkdown(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_linkup(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_disabling(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_disabling_qwait(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_toggling_qwait(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_disabled(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_stopped(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_iocdown(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_iocfail(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void	bfa_fcport_sm_dport(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_ddport(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void	bfa_fcport_sm_faa_misconfig(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);

static void     bfa_fcport_ln_sm_dn(struct bfa_fcport_ln_s *ln,
					enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_dn_nf(struct bfa_fcport_ln_s *ln,
					enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_dn_up_nf(struct bfa_fcport_ln_s *ln,
					enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_up(struct bfa_fcport_ln_s *ln,
					enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_up_nf(struct bfa_fcport_ln_s *ln,
					enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_up_dn_nf(struct bfa_fcport_ln_s *ln,
					enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_up_dn_up_nf(struct bfa_fcport_ln_s *ln,
					enum bfa_fcport_ln_sm_event event);

static struct bfa_sm_table_s hal_port_sm_table[] = {
	{BFA_SM(bfa_fcport_sm_uninit), BFA_PORT_ST_UNINIT},
	{BFA_SM(bfa_fcport_sm_enabling_qwait), BFA_PORT_ST_ENABLING_QWAIT},
	{BFA_SM(bfa_fcport_sm_enabling), BFA_PORT_ST_ENABLING},
	{BFA_SM(bfa_fcport_sm_linkdown), BFA_PORT_ST_LINKDOWN},
	{BFA_SM(bfa_fcport_sm_linkup), BFA_PORT_ST_LINKUP},
	{BFA_SM(bfa_fcport_sm_disabling_qwait), BFA_PORT_ST_DISABLING_QWAIT},
	{BFA_SM(bfa_fcport_sm_toggling_qwait), BFA_PORT_ST_TOGGLING_QWAIT},
	{BFA_SM(bfa_fcport_sm_disabling), BFA_PORT_ST_DISABLING},
	{BFA_SM(bfa_fcport_sm_disabled), BFA_PORT_ST_DISABLED},
	{BFA_SM(bfa_fcport_sm_stopped), BFA_PORT_ST_STOPPED},
	{BFA_SM(bfa_fcport_sm_iocdown), BFA_PORT_ST_IOCDOWN},
	{BFA_SM(bfa_fcport_sm_iocfail), BFA_PORT_ST_IOCDOWN},
	{BFA_SM(bfa_fcport_sm_dport), BFA_PORT_ST_DPORT},
	{BFA_SM(bfa_fcport_sm_ddport), BFA_PORT_ST_DDPORT},
	{BFA_SM(bfa_fcport_sm_faa_misconfig), BFA_PORT_ST_FAA_MISCONFIG},
};


/*
 * forward declaration for RPORT related functions
 */
static struct bfa_rport_s *bfa_rport_alloc(struct bfa_rport_mod_s *rp_mod);
static void		bfa_rport_free(struct bfa_rport_s *rport);
static bfa_boolean_t	bfa_rport_send_fwcreate(struct bfa_rport_s *rp);
static bfa_boolean_t	bfa_rport_send_fwdelete(struct bfa_rport_s *rp);
static bfa_boolean_t	bfa_rport_send_fwspeed(struct bfa_rport_s *rp);
static void		__bfa_cb_rport_online(void *cbarg,
						bfa_boolean_t complete);
static void		__bfa_cb_rport_offline(void *cbarg,
						bfa_boolean_t complete);

/*
 * forward declaration for RPORT state machine
 */
static void     bfa_rport_sm_uninit(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_created(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_fwcreate(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_online(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_fwdelete(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_offline(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_deleting(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_offline_pending(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_delete_pending(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_iocdisable(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_fwcreate_qfull(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_fwdelete_qfull(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void     bfa_rport_sm_deleting_qfull(struct bfa_rport_s *rp,
					enum bfa_rport_event event);

/*
 * PLOG related definitions
 */
static int
plkd_validate_logrec(struct bfa_plog_rec_s *pl_rec)
{
	if ((pl_rec->log_type != BFA_PL_LOG_TYPE_INT) &&
		(pl_rec->log_type != BFA_PL_LOG_TYPE_STRING))
		return 1;

	if ((pl_rec->log_type != BFA_PL_LOG_TYPE_INT) &&
		(pl_rec->log_num_ints > BFA_PL_INT_LOG_SZ))
		return 1;

	return 0;
}

static void
bfa_plog_add(struct bfa_plog_s *plog, struct bfa_plog_rec_s *pl_rec)
{
	u16 tail;
	struct bfa_plog_rec_s *pl_recp;

	if (plog->plog_enabled == 0)
		return;

	if (plkd_validate_logrec(pl_rec)) {
		WARN_ON(1);
		return;
	}

	tail = plog->tail;

	pl_recp = &(plog->plog_recs[tail]);

	memcpy(pl_recp, pl_rec, sizeof(struct bfa_plog_rec_s));

	pl_recp->tv = ktime_get_real_seconds();
	BFA_PL_LOG_REC_INCR(plog->tail);

	if (plog->head == plog->tail)
		BFA_PL_LOG_REC_INCR(plog->head);
}

void
bfa_plog_init(struct bfa_plog_s *plog)
{
	memset((char *)plog, 0, sizeof(struct bfa_plog_s));

	memcpy(plog->plog_sig, BFA_PL_SIG_STR, BFA_PL_SIG_LEN);
	plog->head = plog->tail = 0;
	plog->plog_enabled = 1;
}

void
bfa_plog_str(struct bfa_plog_s *plog, enum bfa_plog_mid mid,
		enum bfa_plog_eid event,
		u16 misc, char *log_str)
{
	struct bfa_plog_rec_s  lp;

	if (plog->plog_enabled) {
		memset(&lp, 0, sizeof(struct bfa_plog_rec_s));
		lp.mid = mid;
		lp.eid = event;
		lp.log_type = BFA_PL_LOG_TYPE_STRING;
		lp.misc = misc;
		strlcpy(lp.log_entry.string_log, log_str,
			BFA_PL_STRING_LOG_SZ);
		lp.log_entry.string_log[BFA_PL_STRING_LOG_SZ - 1] = '\0';
		bfa_plog_add(plog, &lp);
	}
}

void
bfa_plog_intarr(struct bfa_plog_s *plog, enum bfa_plog_mid mid,
		enum bfa_plog_eid event,
		u16 misc, u32 *intarr, u32 num_ints)
{
	struct bfa_plog_rec_s  lp;
	u32 i;

	if (num_ints > BFA_PL_INT_LOG_SZ)
		num_ints = BFA_PL_INT_LOG_SZ;

	if (plog->plog_enabled) {
		memset(&lp, 0, sizeof(struct bfa_plog_rec_s));
		lp.mid = mid;
		lp.eid = event;
		lp.log_type = BFA_PL_LOG_TYPE_INT;
		lp.misc = misc;

		for (i = 0; i < num_ints; i++)
			lp.log_entry.int_log[i] = intarr[i];

		lp.log_num_ints = (u8) num_ints;

		bfa_plog_add(plog, &lp);
	}
}

void
bfa_plog_fchdr(struct bfa_plog_s *plog, enum bfa_plog_mid mid,
			enum bfa_plog_eid event,
			u16 misc, struct fchs_s *fchdr)
{
	struct bfa_plog_rec_s  lp;
	u32	*tmp_int = (u32 *) fchdr;
	u32	ints[BFA_PL_INT_LOG_SZ];

	if (plog->plog_enabled) {
		memset(&lp, 0, sizeof(struct bfa_plog_rec_s));

		ints[0] = tmp_int[0];
		ints[1] = tmp_int[1];
		ints[2] = tmp_int[4];

		bfa_plog_intarr(plog, mid, event, misc, ints, 3);
	}
}

void
bfa_plog_fchdr_and_pl(struct bfa_plog_s *plog, enum bfa_plog_mid mid,
		      enum bfa_plog_eid event, u16 misc, struct fchs_s *fchdr,
		      u32 pld_w0)
{
	struct bfa_plog_rec_s  lp;
	u32	*tmp_int = (u32 *) fchdr;
	u32	ints[BFA_PL_INT_LOG_SZ];

	if (plog->plog_enabled) {
		memset(&lp, 0, sizeof(struct bfa_plog_rec_s));

		ints[0] = tmp_int[0];
		ints[1] = tmp_int[1];
		ints[2] = tmp_int[4];
		ints[3] = pld_w0;

		bfa_plog_intarr(plog, mid, event, misc, ints, 4);
	}
}


/*
 *  fcxp_pvt BFA FCXP private functions
 */

static void
claim_fcxps_mem(struct bfa_fcxp_mod_s *mod)
{
	u16	i;
	struct bfa_fcxp_s *fcxp;

	fcxp = (struct bfa_fcxp_s *) bfa_mem_kva_curp(mod);
	memset(fcxp, 0, sizeof(struct bfa_fcxp_s) * mod->num_fcxps);

	INIT_LIST_HEAD(&mod->fcxp_req_free_q);
	INIT_LIST_HEAD(&mod->fcxp_rsp_free_q);
	INIT_LIST_HEAD(&mod->fcxp_active_q);
	INIT_LIST_HEAD(&mod->fcxp_req_unused_q);
	INIT_LIST_HEAD(&mod->fcxp_rsp_unused_q);

	mod->fcxp_list = fcxp;

	for (i = 0; i < mod->num_fcxps; i++) {
		fcxp->fcxp_mod = mod;
		fcxp->fcxp_tag = i;

		if (i < (mod->num_fcxps / 2)) {
			list_add_tail(&fcxp->qe, &mod->fcxp_req_free_q);
			fcxp->req_rsp = BFA_TRUE;
		} else {
			list_add_tail(&fcxp->qe, &mod->fcxp_rsp_free_q);
			fcxp->req_rsp = BFA_FALSE;
		}

		bfa_reqq_winit(&fcxp->reqq_wqe, bfa_fcxp_qresume, fcxp);
		fcxp->reqq_waiting = BFA_FALSE;

		fcxp = fcxp + 1;
	}

	bfa_mem_kva_curp(mod) = (void *)fcxp;
}

void
bfa_fcxp_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *minfo,
		struct bfa_s *bfa)
{
	struct bfa_fcxp_mod_s *fcxp_mod = BFA_FCXP_MOD(bfa);
	struct bfa_mem_kva_s *fcxp_kva = BFA_MEM_FCXP_KVA(bfa);
	struct bfa_mem_dma_s *seg_ptr;
	u16	nsegs, idx, per_seg_fcxp;
	u16	num_fcxps = cfg->fwcfg.num_fcxp_reqs;
	u32	per_fcxp_sz;

	if (num_fcxps == 0)
		return;

	if (cfg->drvcfg.min_cfg)
		per_fcxp_sz = 2 * BFA_FCXP_MAX_IBUF_SZ;
	else
		per_fcxp_sz = BFA_FCXP_MAX_IBUF_SZ + BFA_FCXP_MAX_LBUF_SZ;

	/* dma memory */
	nsegs = BFI_MEM_DMA_NSEGS(num_fcxps, per_fcxp_sz);
	per_seg_fcxp = BFI_MEM_NREQS_SEG(per_fcxp_sz);

	bfa_mem_dma_seg_iter(fcxp_mod, seg_ptr, nsegs, idx) {
		if (num_fcxps >= per_seg_fcxp) {
			num_fcxps -= per_seg_fcxp;
			bfa_mem_dma_setup(minfo, seg_ptr,
				per_seg_fcxp * per_fcxp_sz);
		} else
			bfa_mem_dma_setup(minfo, seg_ptr,
				num_fcxps * per_fcxp_sz);
	}

	/* kva memory */
	bfa_mem_kva_setup(minfo, fcxp_kva,
		cfg->fwcfg.num_fcxp_reqs * sizeof(struct bfa_fcxp_s));
}

void
bfa_fcxp_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_pcidev_s *pcidev)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);

	mod->bfa = bfa;
	mod->num_fcxps = cfg->fwcfg.num_fcxp_reqs;

	/*
	 * Initialize FCXP request and response payload sizes.
	 */
	mod->req_pld_sz = mod->rsp_pld_sz = BFA_FCXP_MAX_IBUF_SZ;
	if (!cfg->drvcfg.min_cfg)
		mod->rsp_pld_sz = BFA_FCXP_MAX_LBUF_SZ;

	INIT_LIST_HEAD(&mod->req_wait_q);
	INIT_LIST_HEAD(&mod->rsp_wait_q);

	claim_fcxps_mem(mod);
}

void
bfa_fcxp_iocdisable(struct bfa_s *bfa)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);
	struct bfa_fcxp_s *fcxp;
	struct list_head	      *qe, *qen;

	/* Enqueue unused fcxp resources to free_q */
	list_splice_tail_init(&mod->fcxp_req_unused_q, &mod->fcxp_req_free_q);
	list_splice_tail_init(&mod->fcxp_rsp_unused_q, &mod->fcxp_rsp_free_q);

	list_for_each_safe(qe, qen, &mod->fcxp_active_q) {
		fcxp = (struct bfa_fcxp_s *) qe;
		if (fcxp->caller == NULL) {
			fcxp->send_cbfn(fcxp->caller, fcxp, fcxp->send_cbarg,
					BFA_STATUS_IOC_FAILURE, 0, 0, NULL);
			bfa_fcxp_free(fcxp);
		} else {
			fcxp->rsp_status = BFA_STATUS_IOC_FAILURE;
			bfa_cb_queue(bfa, &fcxp->hcb_qe,
				     __bfa_fcxp_send_cbfn, fcxp);
		}
	}
}

static struct bfa_fcxp_s *
bfa_fcxp_get(struct bfa_fcxp_mod_s *fm, bfa_boolean_t req)
{
	struct bfa_fcxp_s *fcxp;

	if (req)
		bfa_q_deq(&fm->fcxp_req_free_q, &fcxp);
	else
		bfa_q_deq(&fm->fcxp_rsp_free_q, &fcxp);

	if (fcxp)
		list_add_tail(&fcxp->qe, &fm->fcxp_active_q);

	return fcxp;
}

static void
bfa_fcxp_init_reqrsp(struct bfa_fcxp_s *fcxp,
	       struct bfa_s *bfa,
	       u8 *use_ibuf,
	       u32 *nr_sgles,
	       bfa_fcxp_get_sgaddr_t *r_sga_cbfn,
	       bfa_fcxp_get_sglen_t *r_sglen_cbfn,
	       struct list_head *r_sgpg_q,
	       int n_sgles,
	       bfa_fcxp_get_sgaddr_t sga_cbfn,
	       bfa_fcxp_get_sglen_t sglen_cbfn)
{

	WARN_ON(bfa == NULL);

	bfa_trc(bfa, fcxp->fcxp_tag);

	if (n_sgles == 0) {
		*use_ibuf = 1;
	} else {
		WARN_ON(*sga_cbfn == NULL);
		WARN_ON(*sglen_cbfn == NULL);

		*use_ibuf = 0;
		*r_sga_cbfn = sga_cbfn;
		*r_sglen_cbfn = sglen_cbfn;

		*nr_sgles = n_sgles;

		/*
		 * alloc required sgpgs
		 */
		if (n_sgles > BFI_SGE_INLINE)
			WARN_ON(1);
	}

}

static void
bfa_fcxp_init(struct bfa_fcxp_s *fcxp,
	       void *caller, struct bfa_s *bfa, int nreq_sgles,
	       int nrsp_sgles, bfa_fcxp_get_sgaddr_t req_sga_cbfn,
	       bfa_fcxp_get_sglen_t req_sglen_cbfn,
	       bfa_fcxp_get_sgaddr_t rsp_sga_cbfn,
	       bfa_fcxp_get_sglen_t rsp_sglen_cbfn)
{

	WARN_ON(bfa == NULL);

	bfa_trc(bfa, fcxp->fcxp_tag);

	fcxp->caller = caller;

	bfa_fcxp_init_reqrsp(fcxp, bfa,
		&fcxp->use_ireqbuf, &fcxp->nreq_sgles, &fcxp->req_sga_cbfn,
		&fcxp->req_sglen_cbfn, &fcxp->req_sgpg_q,
		nreq_sgles, req_sga_cbfn, req_sglen_cbfn);

	bfa_fcxp_init_reqrsp(fcxp, bfa,
		&fcxp->use_irspbuf, &fcxp->nrsp_sgles, &fcxp->rsp_sga_cbfn,
		&fcxp->rsp_sglen_cbfn, &fcxp->rsp_sgpg_q,
		nrsp_sgles, rsp_sga_cbfn, rsp_sglen_cbfn);

}

static void
bfa_fcxp_put(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;
	struct bfa_fcxp_wqe_s *wqe;

	if (fcxp->req_rsp)
		bfa_q_deq(&mod->req_wait_q, &wqe);
	else
		bfa_q_deq(&mod->rsp_wait_q, &wqe);

	if (wqe) {
		bfa_trc(mod->bfa, fcxp->fcxp_tag);

		bfa_fcxp_init(fcxp, wqe->caller, wqe->bfa, wqe->nreq_sgles,
			wqe->nrsp_sgles, wqe->req_sga_cbfn,
			wqe->req_sglen_cbfn, wqe->rsp_sga_cbfn,
			wqe->rsp_sglen_cbfn);

		wqe->alloc_cbfn(wqe->alloc_cbarg, fcxp);
		return;
	}

	WARN_ON(!bfa_q_is_on_q(&mod->fcxp_active_q, fcxp));
	list_del(&fcxp->qe);

	if (fcxp->req_rsp)
		list_add_tail(&fcxp->qe, &mod->fcxp_req_free_q);
	else
		list_add_tail(&fcxp->qe, &mod->fcxp_rsp_free_q);
}

static void
bfa_fcxp_null_comp(void *bfad_fcxp, struct bfa_fcxp_s *fcxp, void *cbarg,
		   bfa_status_t req_status, u32 rsp_len,
		   u32 resid_len, struct fchs_s *rsp_fchs)
{
	/* discarded fcxp completion */
}

static void
__bfa_fcxp_send_cbfn(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_fcxp_s *fcxp = cbarg;

	if (complete) {
		fcxp->send_cbfn(fcxp->caller, fcxp, fcxp->send_cbarg,
				fcxp->rsp_status, fcxp->rsp_len,
				fcxp->residue_len, &fcxp->rsp_fchs);
	} else {
		bfa_fcxp_free(fcxp);
	}
}

static void
hal_fcxp_send_comp(struct bfa_s *bfa, struct bfi_fcxp_send_rsp_s *fcxp_rsp)
{
	struct bfa_fcxp_mod_s	*mod = BFA_FCXP_MOD(bfa);
	struct bfa_fcxp_s	*fcxp;
	u16		fcxp_tag = be16_to_cpu(fcxp_rsp->fcxp_tag);

	bfa_trc(bfa, fcxp_tag);

	fcxp_rsp->rsp_len = be32_to_cpu(fcxp_rsp->rsp_len);

	/*
	 * @todo f/w should not set residue to non-0 when everything
	 *	 is received.
	 */
	if (fcxp_rsp->req_status == BFA_STATUS_OK)
		fcxp_rsp->residue_len = 0;
	else
		fcxp_rsp->residue_len = be32_to_cpu(fcxp_rsp->residue_len);

	fcxp = BFA_FCXP_FROM_TAG(mod, fcxp_tag);

	WARN_ON(fcxp->send_cbfn == NULL);

	hal_fcxp_rx_plog(mod->bfa, fcxp, fcxp_rsp);

	if (fcxp->send_cbfn != NULL) {
		bfa_trc(mod->bfa, (NULL == fcxp->caller));
		if (fcxp->caller == NULL) {
			fcxp->send_cbfn(fcxp->caller, fcxp, fcxp->send_cbarg,
					fcxp_rsp->req_status, fcxp_rsp->rsp_len,
					fcxp_rsp->residue_len, &fcxp_rsp->fchs);
			/*
			 * fcxp automatically freed on return from the callback
			 */
			bfa_fcxp_free(fcxp);
		} else {
			fcxp->rsp_status = fcxp_rsp->req_status;
			fcxp->rsp_len = fcxp_rsp->rsp_len;
			fcxp->residue_len = fcxp_rsp->residue_len;
			fcxp->rsp_fchs = fcxp_rsp->fchs;

			bfa_cb_queue(bfa, &fcxp->hcb_qe,
					__bfa_fcxp_send_cbfn, fcxp);
		}
	} else {
		bfa_trc(bfa, (NULL == fcxp->send_cbfn));
	}
}

static void
hal_fcxp_tx_plog(struct bfa_s *bfa, u32 reqlen, struct bfa_fcxp_s *fcxp,
		 struct fchs_s *fchs)
{
	/*
	 * TODO: TX ox_id
	 */
	if (reqlen > 0) {
		if (fcxp->use_ireqbuf) {
			u32	pld_w0 =
				*((u32 *) BFA_FCXP_REQ_PLD(fcxp));

			bfa_plog_fchdr_and_pl(bfa->plog, BFA_PL_MID_HAL_FCXP,
					BFA_PL_EID_TX,
					reqlen + sizeof(struct fchs_s), fchs,
					pld_w0);
		} else {
			bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_FCXP,
					BFA_PL_EID_TX,
					reqlen + sizeof(struct fchs_s),
					fchs);
		}
	} else {
		bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_FCXP, BFA_PL_EID_TX,
			       reqlen + sizeof(struct fchs_s), fchs);
	}
}

static void
hal_fcxp_rx_plog(struct bfa_s *bfa, struct bfa_fcxp_s *fcxp,
		 struct bfi_fcxp_send_rsp_s *fcxp_rsp)
{
	if (fcxp_rsp->rsp_len > 0) {
		if (fcxp->use_irspbuf) {
			u32	pld_w0 =
				*((u32 *) BFA_FCXP_RSP_PLD(fcxp));

			bfa_plog_fchdr_and_pl(bfa->plog, BFA_PL_MID_HAL_FCXP,
					      BFA_PL_EID_RX,
					      (u16) fcxp_rsp->rsp_len,
					      &fcxp_rsp->fchs, pld_w0);
		} else {
			bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_FCXP,
				       BFA_PL_EID_RX,
				       (u16) fcxp_rsp->rsp_len,
				       &fcxp_rsp->fchs);
		}
	} else {
		bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_FCXP, BFA_PL_EID_RX,
			       (u16) fcxp_rsp->rsp_len, &fcxp_rsp->fchs);
	}
}

/*
 * Handler to resume sending fcxp when space in available in cpe queue.
 */
static void
bfa_fcxp_qresume(void *cbarg)
{
	struct bfa_fcxp_s		*fcxp = cbarg;
	struct bfa_s			*bfa = fcxp->fcxp_mod->bfa;
	struct bfi_fcxp_send_req_s	*send_req;

	fcxp->reqq_waiting = BFA_FALSE;
	send_req = bfa_reqq_next(bfa, BFA_REQQ_FCXP);
	bfa_fcxp_queue(fcxp, send_req);
}

/*
 * Queue fcxp send request to foimrware.
 */
static void
bfa_fcxp_queue(struct bfa_fcxp_s *fcxp, struct bfi_fcxp_send_req_s *send_req)
{
	struct bfa_s			*bfa = fcxp->fcxp_mod->bfa;
	struct bfa_fcxp_req_info_s	*reqi = &fcxp->req_info;
	struct bfa_fcxp_rsp_info_s	*rspi = &fcxp->rsp_info;
	struct bfa_rport_s		*rport = reqi->bfa_rport;

	bfi_h2i_set(send_req->mh, BFI_MC_FCXP, BFI_FCXP_H2I_SEND_REQ,
		    bfa_fn_lpu(bfa));

	send_req->fcxp_tag = cpu_to_be16(fcxp->fcxp_tag);
	if (rport) {
		send_req->rport_fw_hndl = rport->fw_handle;
		send_req->max_frmsz = cpu_to_be16(rport->rport_info.max_frmsz);
		if (send_req->max_frmsz == 0)
			send_req->max_frmsz = cpu_to_be16(FC_MAX_PDUSZ);
	} else {
		send_req->rport_fw_hndl = 0;
		send_req->max_frmsz = cpu_to_be16(FC_MAX_PDUSZ);
	}

	send_req->vf_id = cpu_to_be16(reqi->vf_id);
	send_req->lp_fwtag = bfa_lps_get_fwtag(bfa, reqi->lp_tag);
	send_req->class = reqi->class;
	send_req->rsp_timeout = rspi->rsp_timeout;
	send_req->cts = reqi->cts;
	send_req->fchs = reqi->fchs;

	send_req->req_len = cpu_to_be32(reqi->req_tot_len);
	send_req->rsp_maxlen = cpu_to_be32(rspi->rsp_maxlen);

	/*
	 * setup req sgles
	 */
	if (fcxp->use_ireqbuf == 1) {
		bfa_alen_set(&send_req->req_alen, reqi->req_tot_len,
					BFA_FCXP_REQ_PLD_PA(fcxp));
	} else {
		if (fcxp->nreq_sgles > 0) {
			WARN_ON(fcxp->nreq_sgles != 1);
			bfa_alen_set(&send_req->req_alen, reqi->req_tot_len,
				fcxp->req_sga_cbfn(fcxp->caller, 0));
		} else {
			WARN_ON(reqi->req_tot_len != 0);
			bfa_alen_set(&send_req->rsp_alen, 0, 0);
		}
	}

	/*
	 * setup rsp sgles
	 */
	if (fcxp->use_irspbuf == 1) {
		WARN_ON(rspi->rsp_maxlen > BFA_FCXP_MAX_LBUF_SZ);

		bfa_alen_set(&send_req->rsp_alen, rspi->rsp_maxlen,
					BFA_FCXP_RSP_PLD_PA(fcxp));
	} else {
		if (fcxp->nrsp_sgles > 0) {
			WARN_ON(fcxp->nrsp_sgles != 1);
			bfa_alen_set(&send_req->rsp_alen, rspi->rsp_maxlen,
				fcxp->rsp_sga_cbfn(fcxp->caller, 0));

		} else {
			WARN_ON(rspi->rsp_maxlen != 0);
			bfa_alen_set(&send_req->rsp_alen, 0, 0);
		}
	}

	hal_fcxp_tx_plog(bfa, reqi->req_tot_len, fcxp, &reqi->fchs);

	bfa_reqq_produce(bfa, BFA_REQQ_FCXP, send_req->mh);

	bfa_trc(bfa, bfa_reqq_pi(bfa, BFA_REQQ_FCXP));
	bfa_trc(bfa, bfa_reqq_ci(bfa, BFA_REQQ_FCXP));
}

/*
 * Allocate an FCXP instance to send a response or to send a request
 * that has a response. Request/response buffers are allocated by caller.
 *
 * @param[in]	bfa		BFA bfa instance
 * @param[in]	nreq_sgles	Number of SG elements required for request
 *				buffer. 0, if fcxp internal buffers are	used.
 *				Use bfa_fcxp_get_reqbuf() to get the
 *				internal req buffer.
 * @param[in]	req_sgles	SG elements describing request buffer. Will be
 *				copied in by BFA and hence can be freed on
 *				return from this function.
 * @param[in]	get_req_sga	function ptr to be called to get a request SG
 *				Address (given the sge index).
 * @param[in]	get_req_sglen	function ptr to be called to get a request SG
 *				len (given the sge index).
 * @param[in]	get_rsp_sga	function ptr to be called to get a response SG
 *				Address (given the sge index).
 * @param[in]	get_rsp_sglen	function ptr to be called to get a response SG
 *				len (given the sge index).
 * @param[in]	req		Allocated FCXP is used to send req or rsp?
 *				request - BFA_TRUE, response - BFA_FALSE
 *
 * @return FCXP instance. NULL on failure.
 */
struct bfa_fcxp_s *
bfa_fcxp_req_rsp_alloc(void *caller, struct bfa_s *bfa, int nreq_sgles,
		int nrsp_sgles, bfa_fcxp_get_sgaddr_t req_sga_cbfn,
		bfa_fcxp_get_sglen_t req_sglen_cbfn,
		bfa_fcxp_get_sgaddr_t rsp_sga_cbfn,
		bfa_fcxp_get_sglen_t rsp_sglen_cbfn, bfa_boolean_t req)
{
	struct bfa_fcxp_s *fcxp = NULL;

	WARN_ON(bfa == NULL);

	fcxp = bfa_fcxp_get(BFA_FCXP_MOD(bfa), req);
	if (fcxp == NULL)
		return NULL;

	bfa_trc(bfa, fcxp->fcxp_tag);

	bfa_fcxp_init(fcxp, caller, bfa, nreq_sgles, nrsp_sgles, req_sga_cbfn,
			req_sglen_cbfn, rsp_sga_cbfn, rsp_sglen_cbfn);

	return fcxp;
}

/*
 * Get the internal request buffer pointer
 *
 * @param[in]	fcxp	BFA fcxp pointer
 *
 * @return		pointer to the internal request buffer
 */
void *
bfa_fcxp_get_reqbuf(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;
	void	*reqbuf;

	WARN_ON(fcxp->use_ireqbuf != 1);
	reqbuf = bfa_mem_get_dmabuf_kva(mod, fcxp->fcxp_tag,
				mod->req_pld_sz + mod->rsp_pld_sz);
	return reqbuf;
}

u32
bfa_fcxp_get_reqbufsz(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;

	return mod->req_pld_sz;
}

/*
 * Get the internal response buffer pointer
 *
 * @param[in]	fcxp	BFA fcxp pointer
 *
 * @return		pointer to the internal request buffer
 */
void *
bfa_fcxp_get_rspbuf(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;
	void	*fcxp_buf;

	WARN_ON(fcxp->use_irspbuf != 1);

	fcxp_buf = bfa_mem_get_dmabuf_kva(mod, fcxp->fcxp_tag,
				mod->req_pld_sz + mod->rsp_pld_sz);

	/* fcxp_buf = req_buf + rsp_buf :- add req_buf_sz to get to rsp_buf */
	return ((u8 *) fcxp_buf) + mod->req_pld_sz;
}

/*
 * Free the BFA FCXP
 *
 * @param[in]	fcxp			BFA fcxp pointer
 *
 * @return		void
 */
void
bfa_fcxp_free(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;

	WARN_ON(fcxp == NULL);
	bfa_trc(mod->bfa, fcxp->fcxp_tag);
	bfa_fcxp_put(fcxp);
}

/*
 * Send a FCXP request
 *
 * @param[in]	fcxp	BFA fcxp pointer
 * @param[in]	rport	BFA rport pointer. Could be left NULL for WKA rports
 * @param[in]	vf_id	virtual Fabric ID
 * @param[in]	lp_tag	lport tag
 * @param[in]	cts	use Continuous sequence
 * @param[in]	cos	fc Class of Service
 * @param[in]	reqlen	request length, does not include FCHS length
 * @param[in]	fchs	fc Header Pointer. The header content will be copied
 *			in by BFA.
 *
 * @param[in]	cbfn	call back function to be called on receiving
 *								the response
 * @param[in]	cbarg	arg for cbfn
 * @param[in]	rsp_timeout
 *			response timeout
 *
 * @return		bfa_status_t
 */
void
bfa_fcxp_send(struct bfa_fcxp_s *fcxp, struct bfa_rport_s *rport,
	      u16 vf_id, u8 lp_tag, bfa_boolean_t cts, enum fc_cos cos,
	      u32 reqlen, struct fchs_s *fchs, bfa_cb_fcxp_send_t cbfn,
	      void *cbarg, u32 rsp_maxlen, u8 rsp_timeout)
{
	struct bfa_s			*bfa  = fcxp->fcxp_mod->bfa;
	struct bfa_fcxp_req_info_s	*reqi = &fcxp->req_info;
	struct bfa_fcxp_rsp_info_s	*rspi = &fcxp->rsp_info;
	struct bfi_fcxp_send_req_s	*send_req;

	bfa_trc(bfa, fcxp->fcxp_tag);

	/*
	 * setup request/response info
	 */
	reqi->bfa_rport = rport;
	reqi->vf_id = vf_id;
	reqi->lp_tag = lp_tag;
	reqi->class = cos;
	rspi->rsp_timeout = rsp_timeout;
	reqi->cts = cts;
	reqi->fchs = *fchs;
	reqi->req_tot_len = reqlen;
	rspi->rsp_maxlen = rsp_maxlen;
	fcxp->send_cbfn = cbfn ? cbfn : bfa_fcxp_null_comp;
	fcxp->send_cbarg = cbarg;

	/*
	 * If no room in CPE queue, wait for space in request queue
	 */
	send_req = bfa_reqq_next(bfa, BFA_REQQ_FCXP);
	if (!send_req) {
		bfa_trc(bfa, fcxp->fcxp_tag);
		fcxp->reqq_waiting = BFA_TRUE;
		bfa_reqq_wait(bfa, BFA_REQQ_FCXP, &fcxp->reqq_wqe);
		return;
	}

	bfa_fcxp_queue(fcxp, send_req);
}

/*
 * Abort a BFA FCXP
 *
 * @param[in]	fcxp	BFA fcxp pointer
 *
 * @return		void
 */
bfa_status_t
bfa_fcxp_abort(struct bfa_fcxp_s *fcxp)
{
	bfa_trc(fcxp->fcxp_mod->bfa, fcxp->fcxp_tag);
	WARN_ON(1);
	return BFA_STATUS_OK;
}

void
bfa_fcxp_req_rsp_alloc_wait(struct bfa_s *bfa, struct bfa_fcxp_wqe_s *wqe,
	       bfa_fcxp_alloc_cbfn_t alloc_cbfn, void *alloc_cbarg,
	       void *caller, int nreq_sgles,
	       int nrsp_sgles, bfa_fcxp_get_sgaddr_t req_sga_cbfn,
	       bfa_fcxp_get_sglen_t req_sglen_cbfn,
	       bfa_fcxp_get_sgaddr_t rsp_sga_cbfn,
	       bfa_fcxp_get_sglen_t rsp_sglen_cbfn, bfa_boolean_t req)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);

	if (req)
		WARN_ON(!list_empty(&mod->fcxp_req_free_q));
	else
		WARN_ON(!list_empty(&mod->fcxp_rsp_free_q));

	wqe->alloc_cbfn = alloc_cbfn;
	wqe->alloc_cbarg = alloc_cbarg;
	wqe->caller = caller;
	wqe->bfa = bfa;
	wqe->nreq_sgles = nreq_sgles;
	wqe->nrsp_sgles = nrsp_sgles;
	wqe->req_sga_cbfn = req_sga_cbfn;
	wqe->req_sglen_cbfn = req_sglen_cbfn;
	wqe->rsp_sga_cbfn = rsp_sga_cbfn;
	wqe->rsp_sglen_cbfn = rsp_sglen_cbfn;

	if (req)
		list_add_tail(&wqe->qe, &mod->req_wait_q);
	else
		list_add_tail(&wqe->qe, &mod->rsp_wait_q);
}

void
bfa_fcxp_walloc_cancel(struct bfa_s *bfa, struct bfa_fcxp_wqe_s *wqe)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);

	WARN_ON(!bfa_q_is_on_q(&mod->req_wait_q, wqe) ||
		!bfa_q_is_on_q(&mod->rsp_wait_q, wqe));
	list_del(&wqe->qe);
}

void
bfa_fcxp_discard(struct bfa_fcxp_s *fcxp)
{
	/*
	 * If waiting for room in request queue, cancel reqq wait
	 * and free fcxp.
	 */
	if (fcxp->reqq_waiting) {
		fcxp->reqq_waiting = BFA_FALSE;
		bfa_reqq_wcancel(&fcxp->reqq_wqe);
		bfa_fcxp_free(fcxp);
		return;
	}

	fcxp->send_cbfn = bfa_fcxp_null_comp;
}

void
bfa_fcxp_isr(struct bfa_s *bfa, struct bfi_msg_s *msg)
{
	switch (msg->mhdr.msg_id) {
	case BFI_FCXP_I2H_SEND_RSP:
		hal_fcxp_send_comp(bfa, (struct bfi_fcxp_send_rsp_s *) msg);
		break;

	default:
		bfa_trc(bfa, msg->mhdr.msg_id);
		WARN_ON(1);
	}
}

u32
bfa_fcxp_get_maxrsp(struct bfa_s *bfa)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);

	return mod->rsp_pld_sz;
}

void
bfa_fcxp_res_recfg(struct bfa_s *bfa, u16 num_fcxp_fw)
{
	struct bfa_fcxp_mod_s	*mod = BFA_FCXP_MOD(bfa);
	struct list_head	*qe;
	int	i;

	for (i = 0; i < (mod->num_fcxps - num_fcxp_fw); i++) {
		if (i < ((mod->num_fcxps - num_fcxp_fw) / 2)) {
			bfa_q_deq_tail(&mod->fcxp_req_free_q, &qe);
			list_add_tail(qe, &mod->fcxp_req_unused_q);
		} else {
			bfa_q_deq_tail(&mod->fcxp_rsp_free_q, &qe);
			list_add_tail(qe, &mod->fcxp_rsp_unused_q);
		}
	}
}

/*
 *  BFA LPS state machine functions
 */

/*
 * Init state -- no login
 */
static void
bfa_lps_sm_init(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->bfa_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_LOGIN:
		if (bfa_reqq_full(lps->bfa, lps->reqq)) {
			bfa_sm_set_state(lps, bfa_lps_sm_loginwait);
			bfa_reqq_wait(lps->bfa, lps->reqq, &lps->wqe);
		} else {
			bfa_sm_set_state(lps, bfa_lps_sm_login);
			bfa_lps_send_login(lps);
		}

		if (lps->fdisc)
			bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
				BFA_PL_EID_LOGIN, 0, "FDISC Request");
		else
			bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
				BFA_PL_EID_LOGIN, 0, "FLOGI Request");
		break;

	case BFA_LPS_SM_LOGOUT:
		bfa_lps_logout_comp(lps);
		break;

	case BFA_LPS_SM_DELETE:
		bfa_lps_free(lps);
		break;

	case BFA_LPS_SM_RX_CVL:
	case BFA_LPS_SM_OFFLINE:
		break;

	case BFA_LPS_SM_FWRSP:
		/*
		 * Could happen when fabric detects loopback and discards
		 * the lps request. Fw will eventually sent out the timeout
		 * Just ignore
		 */
		break;
	case BFA_LPS_SM_SET_N2N_PID:
		/*
		 * When topology is set to loop, bfa_lps_set_n2n_pid() sends
		 * this event. Ignore this event.
		 */
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/*
 * login is in progress -- awaiting response from firmware
 */
static void
bfa_lps_sm_login(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->bfa_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_FWRSP:
		if (lps->status == BFA_STATUS_OK) {
			bfa_sm_set_state(lps, bfa_lps_sm_online);
			if (lps->fdisc)
				bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
					BFA_PL_EID_LOGIN, 0, "FDISC Accept");
			else
				bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
					BFA_PL_EID_LOGIN, 0, "FLOGI Accept");
			/* If N2N, send the assigned PID to FW */
			bfa_trc(lps->bfa, lps->fport);
			bfa_trc(lps->bfa, lps->lp_pid);

			if (!lps->fport && lps->lp_pid)
				bfa_sm_send_event(lps, BFA_LPS_SM_SET_N2N_PID);
		} else {
			bfa_sm_set_state(lps, bfa_lps_sm_init);
			if (lps->fdisc)
				bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
					BFA_PL_EID_LOGIN, 0,
					"FDISC Fail (RJT or timeout)");
			else
				bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
					BFA_PL_EID_LOGIN, 0,
					"FLOGI Fail (RJT or timeout)");
		}
		bfa_lps_login_comp(lps);
		break;

	case BFA_LPS_SM_OFFLINE:
	case BFA_LPS_SM_DELETE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		break;

	case BFA_LPS_SM_SET_N2N_PID:
		bfa_trc(lps->bfa, lps->fport);
		bfa_trc(lps->bfa, lps->lp_pid);
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/*
 * login pending - awaiting space in request queue
 */
static void
bfa_lps_sm_loginwait(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->bfa_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_RESUME:
		bfa_sm_set_state(lps, bfa_lps_sm_login);
		bfa_lps_send_login(lps);
		break;

	case BFA_LPS_SM_OFFLINE:
	case BFA_LPS_SM_DELETE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		bfa_reqq_wcancel(&lps->wqe);
		break;

	case BFA_LPS_SM_RX_CVL:
		/*
		 * Login was not even sent out; so when getting out
		 * of this state, it will appear like a login retry
		 * after Clear virtual link
		 */
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/*
 * login complete
 */
static void
bfa_lps_sm_online(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->bfa_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_LOGOUT:
		if (bfa_reqq_full(lps->bfa, lps->reqq)) {
			bfa_sm_set_state(lps, bfa_lps_sm_logowait);
			bfa_reqq_wait(lps->bfa, lps->reqq, &lps->wqe);
		} else {
			bfa_sm_set_state(lps, bfa_lps_sm_logout);
			bfa_lps_send_logout(lps);
		}
		bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
			BFA_PL_EID_LOGO, 0, "Logout");
		break;

	case BFA_LPS_SM_RX_CVL:
		bfa_sm_set_state(lps, bfa_lps_sm_init);

		/* Let the vport module know about this event */
		bfa_lps_cvl_event(lps);
		bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
			BFA_PL_EID_FIP_FCF_CVL, 0, "FCF Clear Virt. Link Rx");
		break;

	case BFA_LPS_SM_SET_N2N_PID:
		if (bfa_reqq_full(lps->bfa, lps->reqq)) {
			bfa_sm_set_state(lps, bfa_lps_sm_online_n2n_pid_wait);
			bfa_reqq_wait(lps->bfa, lps->reqq, &lps->wqe);
		} else
			bfa_lps_send_set_n2n_pid(lps);
		break;

	case BFA_LPS_SM_OFFLINE:
	case BFA_LPS_SM_DELETE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/*
 * login complete
 */
static void
bfa_lps_sm_online_n2n_pid_wait(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->bfa_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_RESUME:
		bfa_sm_set_state(lps, bfa_lps_sm_online);
		bfa_lps_send_set_n2n_pid(lps);
		break;

	case BFA_LPS_SM_LOGOUT:
		bfa_sm_set_state(lps, bfa_lps_sm_logowait);
		bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
			BFA_PL_EID_LOGO, 0, "Logout");
		break;

	case BFA_LPS_SM_RX_CVL:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		bfa_reqq_wcancel(&lps->wqe);

		/* Let the vport module know about this event */
		bfa_lps_cvl_event(lps);
		bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
			BFA_PL_EID_FIP_FCF_CVL, 0, "FCF Clear Virt. Link Rx");
		break;

	case BFA_LPS_SM_OFFLINE:
	case BFA_LPS_SM_DELETE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		bfa_reqq_wcancel(&lps->wqe);
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/*
 * logout in progress - awaiting firmware response
 */
static void
bfa_lps_sm_logout(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->bfa_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_FWRSP:
	case BFA_LPS_SM_OFFLINE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		bfa_lps_logout_comp(lps);
		break;

	case BFA_LPS_SM_DELETE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/*
 * logout pending -- awaiting space in request queue
 */
static void
bfa_lps_sm_logowait(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->bfa_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_RESUME:
		bfa_sm_set_state(lps, bfa_lps_sm_logout);
		bfa_lps_send_logout(lps);
		break;

	case BFA_LPS_SM_OFFLINE:
	case BFA_LPS_SM_DELETE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		bfa_reqq_wcancel(&lps->wqe);
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}



/*
 *  lps_pvt BFA LPS private functions
 */

/*
 * return memory requirement
 */
void
bfa_lps_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *minfo,
		struct bfa_s *bfa)
{
	struct bfa_mem_kva_s *lps_kva = BFA_MEM_LPS_KVA(bfa);

	if (cfg->drvcfg.min_cfg)
		bfa_mem_kva_setup(minfo, lps_kva,
			sizeof(struct bfa_lps_s) * BFA_LPS_MIN_LPORTS);
	else
		bfa_mem_kva_setup(minfo, lps_kva,
			sizeof(struct bfa_lps_s) * BFA_LPS_MAX_LPORTS);
}

/*
 * bfa module attach at initialization time
 */
void
bfa_lps_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
	struct bfa_pcidev_s *pcidev)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;
	int			i;

	mod->num_lps = BFA_LPS_MAX_LPORTS;
	if (cfg->drvcfg.min_cfg)
		mod->num_lps = BFA_LPS_MIN_LPORTS;
	else
		mod->num_lps = BFA_LPS_MAX_LPORTS;
	mod->lps_arr = lps = (struct bfa_lps_s *) bfa_mem_kva_curp(mod);

	bfa_mem_kva_curp(mod) += mod->num_lps * sizeof(struct bfa_lps_s);

	INIT_LIST_HEAD(&mod->lps_free_q);
	INIT_LIST_HEAD(&mod->lps_active_q);
	INIT_LIST_HEAD(&mod->lps_login_q);

	for (i = 0; i < mod->num_lps; i++, lps++) {
		lps->bfa	= bfa;
		lps->bfa_tag	= (u8) i;
		lps->reqq	= BFA_REQQ_LPS;
		bfa_reqq_winit(&lps->wqe, bfa_lps_reqq_resume, lps);
		list_add_tail(&lps->qe, &mod->lps_free_q);
	}
}

/*
 * IOC in disabled state -- consider all lps offline
 */
void
bfa_lps_iocdisable(struct bfa_s *bfa)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;
	struct list_head		*qe, *qen;

	list_for_each_safe(qe, qen, &mod->lps_active_q) {
		lps = (struct bfa_lps_s *) qe;
		bfa_sm_send_event(lps, BFA_LPS_SM_OFFLINE);
	}
	list_for_each_safe(qe, qen, &mod->lps_login_q) {
		lps = (struct bfa_lps_s *) qe;
		bfa_sm_send_event(lps, BFA_LPS_SM_OFFLINE);
	}
	list_splice_tail_init(&mod->lps_login_q, &mod->lps_active_q);
}

/*
 * Firmware login response
 */
static void
bfa_lps_login_rsp(struct bfa_s *bfa, struct bfi_lps_login_rsp_s *rsp)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;

	WARN_ON(rsp->bfa_tag >= mod->num_lps);
	lps = BFA_LPS_FROM_TAG(mod, rsp->bfa_tag);

	lps->status = rsp->status;
	switch (rsp->status) {
	case BFA_STATUS_OK:
		lps->fw_tag	= rsp->fw_tag;
		lps->fport	= rsp->f_port;
		if (lps->fport)
			lps->lp_pid = rsp->lp_pid;
		lps->npiv_en	= rsp->npiv_en;
		lps->pr_bbcred	= be16_to_cpu(rsp->bb_credit);
		lps->pr_pwwn	= rsp->port_name;
		lps->pr_nwwn	= rsp->node_name;
		lps->auth_req	= rsp->auth_req;
		lps->lp_mac	= rsp->lp_mac;
		lps->brcd_switch = rsp->brcd_switch;
		lps->fcf_mac	= rsp->fcf_mac;

		break;

	case BFA_STATUS_FABRIC_RJT:
		lps->lsrjt_rsn = rsp->lsrjt_rsn;
		lps->lsrjt_expl = rsp->lsrjt_expl;

		break;

	case BFA_STATUS_EPROTOCOL:
		lps->ext_status = rsp->ext_status;

		break;

	case BFA_STATUS_VPORT_MAX:
		if (rsp->ext_status)
			bfa_lps_no_res(lps, rsp->ext_status);
		break;

	default:
		/* Nothing to do with other status */
		break;
	}

	list_del(&lps->qe);
	list_add_tail(&lps->qe, &mod->lps_active_q);
	bfa_sm_send_event(lps, BFA_LPS_SM_FWRSP);
}

static void
bfa_lps_no_res(struct bfa_lps_s *first_lps, u8 count)
{
	struct bfa_s		*bfa = first_lps->bfa;
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct list_head	*qe, *qe_next;
	struct bfa_lps_s	*lps;

	bfa_trc(bfa, count);

	qe = bfa_q_next(first_lps);

	while (count && qe) {
		qe_next = bfa_q_next(qe);
		lps = (struct bfa_lps_s *)qe;
		bfa_trc(bfa, lps->bfa_tag);
		lps->status = first_lps->status;
		list_del(&lps->qe);
		list_add_tail(&lps->qe, &mod->lps_active_q);
		bfa_sm_send_event(lps, BFA_LPS_SM_FWRSP);
		qe = qe_next;
		count--;
	}
}

/*
 * Firmware logout response
 */
static void
bfa_lps_logout_rsp(struct bfa_s *bfa, struct bfi_lps_logout_rsp_s *rsp)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;

	WARN_ON(rsp->bfa_tag >= mod->num_lps);
	lps = BFA_LPS_FROM_TAG(mod, rsp->bfa_tag);

	bfa_sm_send_event(lps, BFA_LPS_SM_FWRSP);
}

/*
 * Firmware received a Clear virtual link request (for FCoE)
 */
static void
bfa_lps_rx_cvl_event(struct bfa_s *bfa, struct bfi_lps_cvl_event_s *cvl)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;

	lps = BFA_LPS_FROM_TAG(mod, cvl->bfa_tag);

	bfa_sm_send_event(lps, BFA_LPS_SM_RX_CVL);
}

/*
 * Space is available in request queue, resume queueing request to firmware.
 */
static void
bfa_lps_reqq_resume(void *lps_arg)
{
	struct bfa_lps_s	*lps = lps_arg;

	bfa_sm_send_event(lps, BFA_LPS_SM_RESUME);
}

/*
 * lps is freed -- triggered by vport delete
 */
static void
bfa_lps_free(struct bfa_lps_s *lps)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(lps->bfa);

	lps->lp_pid = 0;
	list_del(&lps->qe);
	list_add_tail(&lps->qe, &mod->lps_free_q);
}

/*
 * send login request to firmware
 */
static void
bfa_lps_send_login(struct bfa_lps_s *lps)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(lps->bfa);
	struct bfi_lps_login_req_s	*m;

	m = bfa_reqq_next(lps->bfa, lps->reqq);
	WARN_ON(!m);

	bfi_h2i_set(m->mh, BFI_MC_LPS, BFI_LPS_H2I_LOGIN_REQ,
		bfa_fn_lpu(lps->bfa));

	m->bfa_tag	= lps->bfa_tag;
	m->alpa		= lps->alpa;
	m->pdu_size	= cpu_to_be16(lps->pdusz);
	m->pwwn		= lps->pwwn;
	m->nwwn		= lps->nwwn;
	m->fdisc	= lps->fdisc;
	m->auth_en	= lps->auth_en;

	bfa_reqq_produce(lps->bfa, lps->reqq, m->mh);
	list_del(&lps->qe);
	list_add_tail(&lps->qe, &mod->lps_login_q);
}

/*
 * send logout request to firmware
 */
static void
bfa_lps_send_logout(struct bfa_lps_s *lps)
{
	struct bfi_lps_logout_req_s *m;

	m = bfa_reqq_next(lps->bfa, lps->reqq);
	WARN_ON(!m);

	bfi_h2i_set(m->mh, BFI_MC_LPS, BFI_LPS_H2I_LOGOUT_REQ,
		bfa_fn_lpu(lps->bfa));

	m->fw_tag = lps->fw_tag;
	m->port_name = lps->pwwn;
	bfa_reqq_produce(lps->bfa, lps->reqq, m->mh);
}

/*
 * send n2n pid set request to firmware
 */
static void
bfa_lps_send_set_n2n_pid(struct bfa_lps_s *lps)
{
	struct bfi_lps_n2n_pid_req_s *m;

	m = bfa_reqq_next(lps->bfa, lps->reqq);
	WARN_ON(!m);

	bfi_h2i_set(m->mh, BFI_MC_LPS, BFI_LPS_H2I_N2N_PID_REQ,
		bfa_fn_lpu(lps->bfa));

	m->fw_tag = lps->fw_tag;
	m->lp_pid = lps->lp_pid;
	bfa_reqq_produce(lps->bfa, lps->reqq, m->mh);
}

/*
 * Indirect login completion handler for non-fcs
 */
static void
bfa_lps_login_comp_cb(void *arg, bfa_boolean_t complete)
{
	struct bfa_lps_s *lps	= arg;

	if (!complete)
		return;

	if (lps->fdisc)
		bfa_cb_lps_fdisc_comp(lps->bfa->bfad, lps->uarg, lps->status);
	else
		bfa_cb_lps_flogi_comp(lps->bfa->bfad, lps->uarg, lps->status);
}

/*
 * Login completion handler -- direct call for fcs, queue for others
 */
static void
bfa_lps_login_comp(struct bfa_lps_s *lps)
{
	if (!lps->bfa->fcs) {
		bfa_cb_queue(lps->bfa, &lps->hcb_qe, bfa_lps_login_comp_cb,
			lps);
		return;
	}

	if (lps->fdisc)
		bfa_cb_lps_fdisc_comp(lps->bfa->bfad, lps->uarg, lps->status);
	else
		bfa_cb_lps_flogi_comp(lps->bfa->bfad, lps->uarg, lps->status);
}

/*
 * Indirect logout completion handler for non-fcs
 */
static void
bfa_lps_logout_comp_cb(void *arg, bfa_boolean_t complete)
{
	struct bfa_lps_s *lps	= arg;

	if (!complete)
		return;

	if (lps->fdisc)
		bfa_cb_lps_fdisclogo_comp(lps->bfa->bfad, lps->uarg);
	else
		bfa_cb_lps_flogo_comp(lps->bfa->bfad, lps->uarg);
}

/*
 * Logout completion handler -- direct call for fcs, queue for others
 */
static void
bfa_lps_logout_comp(struct bfa_lps_s *lps)
{
	if (!lps->bfa->fcs) {
		bfa_cb_queue(lps->bfa, &lps->hcb_qe, bfa_lps_logout_comp_cb,
			lps);
		return;
	}
	if (lps->fdisc)
		bfa_cb_lps_fdisclogo_comp(lps->bfa->bfad, lps->uarg);
}

/*
 * Clear virtual link completion handler for non-fcs
 */
static void
bfa_lps_cvl_event_cb(void *arg, bfa_boolean_t complete)
{
	struct bfa_lps_s *lps	= arg;

	if (!complete)
		return;

	/* Clear virtual link to base port will result in link down */
	if (lps->fdisc)
		bfa_cb_lps_cvl_event(lps->bfa->bfad, lps->uarg);
}

/*
 * Received Clear virtual link event --direct call for fcs,
 * queue for others
 */
static void
bfa_lps_cvl_event(struct bfa_lps_s *lps)
{
	if (!lps->bfa->fcs) {
		bfa_cb_queue(lps->bfa, &lps->hcb_qe, bfa_lps_cvl_event_cb,
			lps);
		return;
	}

	/* Clear virtual link to base port will result in link down */
	if (lps->fdisc)
		bfa_cb_lps_cvl_event(lps->bfa->bfad, lps->uarg);
}



/*
 *  lps_public BFA LPS public functions
 */

u32
bfa_lps_get_max_vport(struct bfa_s *bfa)
{
	if (bfa_ioc_devid(&bfa->ioc) == BFA_PCI_DEVICE_ID_CT)
		return BFA_LPS_MAX_VPORTS_SUPP_CT;
	else
		return BFA_LPS_MAX_VPORTS_SUPP_CB;
}

/*
 * Allocate a lport srvice tag.
 */
struct bfa_lps_s  *
bfa_lps_alloc(struct bfa_s *bfa)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps = NULL;

	bfa_q_deq(&mod->lps_free_q, &lps);

	if (lps == NULL)
		return NULL;

	list_add_tail(&lps->qe, &mod->lps_active_q);

	bfa_sm_set_state(lps, bfa_lps_sm_init);
	return lps;
}

/*
 * Free lport service tag. This can be called anytime after an alloc.
 * No need to wait for any pending login/logout completions.
 */
void
bfa_lps_delete(struct bfa_lps_s *lps)
{
	bfa_sm_send_event(lps, BFA_LPS_SM_DELETE);
}

/*
 * Initiate a lport login.
 */
void
bfa_lps_flogi(struct bfa_lps_s *lps, void *uarg, u8 alpa, u16 pdusz,
	wwn_t pwwn, wwn_t nwwn, bfa_boolean_t auth_en)
{
	lps->uarg	= uarg;
	lps->alpa	= alpa;
	lps->pdusz	= pdusz;
	lps->pwwn	= pwwn;
	lps->nwwn	= nwwn;
	lps->fdisc	= BFA_FALSE;
	lps->auth_en	= auth_en;
	bfa_sm_send_event(lps, BFA_LPS_SM_LOGIN);
}

/*
 * Initiate a lport fdisc login.
 */
void
bfa_lps_fdisc(struct bfa_lps_s *lps, void *uarg, u16 pdusz, wwn_t pwwn,
	wwn_t nwwn)
{
	lps->uarg	= uarg;
	lps->alpa	= 0;
	lps->pdusz	= pdusz;
	lps->pwwn	= pwwn;
	lps->nwwn	= nwwn;
	lps->fdisc	= BFA_TRUE;
	lps->auth_en	= BFA_FALSE;
	bfa_sm_send_event(lps, BFA_LPS_SM_LOGIN);
}


/*
 * Initiate a lport FDSIC logout.
 */
void
bfa_lps_fdisclogo(struct bfa_lps_s *lps)
{
	bfa_sm_send_event(lps, BFA_LPS_SM_LOGOUT);
}

u8
bfa_lps_get_fwtag(struct bfa_s *bfa, u8 lp_tag)
{
	struct bfa_lps_mod_s    *mod = BFA_LPS_MOD(bfa);

	return BFA_LPS_FROM_TAG(mod, lp_tag)->fw_tag;
}

/*
 * Return lport services tag given the pid
 */
u8
bfa_lps_get_tag_from_pid(struct bfa_s *bfa, u32 pid)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;
	int			i;

	for (i = 0, lps = mod->lps_arr; i < mod->num_lps; i++, lps++) {
		if (lps->lp_pid == pid)
			return lps->bfa_tag;
	}

	/* Return base port tag anyway */
	return 0;
}


/*
 * return port id assigned to the base lport
 */
u32
bfa_lps_get_base_pid(struct bfa_s *bfa)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);

	return BFA_LPS_FROM_TAG(mod, 0)->lp_pid;
}

/*
 * Set PID in case of n2n (which is assigned during PLOGI)
 */
void
bfa_lps_set_n2n_pid(struct bfa_lps_s *lps, uint32_t n2n_pid)
{
	bfa_trc(lps->bfa, lps->bfa_tag);
	bfa_trc(lps->bfa, n2n_pid);

	lps->lp_pid = n2n_pid;
	bfa_sm_send_event(lps, BFA_LPS_SM_SET_N2N_PID);
}

/*
 * LPS firmware message class handler.
 */
void
bfa_lps_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	union bfi_lps_i2h_msg_u	msg;

	bfa_trc(bfa, m->mhdr.msg_id);
	msg.msg = m;

	switch (m->mhdr.msg_id) {
	case BFI_LPS_I2H_LOGIN_RSP:
		bfa_lps_login_rsp(bfa, msg.login_rsp);
		break;

	case BFI_LPS_I2H_LOGOUT_RSP:
		bfa_lps_logout_rsp(bfa, msg.logout_rsp);
		break;

	case BFI_LPS_I2H_CVL_EVENT:
		bfa_lps_rx_cvl_event(bfa, msg.cvl_event);
		break;

	default:
		bfa_trc(bfa, m->mhdr.msg_id);
		WARN_ON(1);
	}
}

static void
bfa_fcport_aen_post(struct bfa_fcport_s *fcport, enum bfa_port_aen_event event)
{
	struct bfad_s *bfad = (struct bfad_s *)fcport->bfa->bfad;
	struct bfa_aen_entry_s  *aen_entry;

	bfad_get_aen_entry(bfad, aen_entry);
	if (!aen_entry)
		return;

	aen_entry->aen_data.port.ioc_type = bfa_get_type(fcport->bfa);
	aen_entry->aen_data.port.pwwn = fcport->pwwn;

	/* Send the AEN notification */
	bfad_im_post_vendor_event(aen_entry, bfad, ++fcport->bfa->bfa_aen_seq,
				  BFA_AEN_CAT_PORT, event);
}

/*
 * FC PORT state machine functions
 */
static void
bfa_fcport_sm_uninit(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		/*
		 * Start event after IOC is configured and BFA is started.
		 */
		fcport->use_flash_cfg = BFA_TRUE;

		if (bfa_fcport_send_enable(fcport)) {
			bfa_trc(fcport->bfa, BFA_TRUE);
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		} else {
			bfa_trc(fcport->bfa, BFA_FALSE);
			bfa_sm_set_state(fcport,
					bfa_fcport_sm_enabling_qwait);
		}
		break;

	case BFA_FCPORT_SM_ENABLE:
		/*
		 * Port is persistently configured to be in enabled state. Do
		 * not change state. Port enabling is done when START event is
		 * received.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		/*
		 * If a port is persistently configured to be disabled, the
		 * first event will a port disable request.
		 */
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_enabling_qwait(struct bfa_fcport_s *fcport,
				enum bfa_fcport_sm_event event)
{
	char pwwn_buf[BFA_STRING_32];
	struct bfad_s *bfad = (struct bfad_s *)fcport->bfa->bfad;
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_QRESUME:
		bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		bfa_fcport_send_enable(fcport);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_reqq_wcancel(&fcport->reqq_wait);
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_ENABLE:
		/*
		 * Already enable is in progress.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		/*
		 * Just send disable request to firmware when room becomes
		 * available in request queue.
		 */
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		bfa_reqq_wcancel(&fcport->reqq_wait);
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
				BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		wwn2str(pwwn_buf, fcport->pwwn);
		BFA_LOG(KERN_INFO, bfad, bfa_log_level,
			"Base port disabled: WWN = %s\n", pwwn_buf);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_FCPORT_SM_LINKUP:
	case BFA_FCPORT_SM_LINKDOWN:
		/*
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_reqq_wcancel(&fcport->reqq_wait);
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	case BFA_FCPORT_SM_FAA_MISCONFIG:
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		bfa_sm_set_state(fcport, bfa_fcport_sm_faa_misconfig);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_enabling(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event)
{
	char pwwn_buf[BFA_STRING_32];
	struct bfad_s *bfad = (struct bfad_s *)fcport->bfa->bfad;
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_FWRSP:
	case BFA_FCPORT_SM_LINKDOWN:
		bfa_sm_set_state(fcport, bfa_fcport_sm_linkdown);
		break;

	case BFA_FCPORT_SM_LINKUP:
		bfa_fcport_update_linkinfo(fcport);
		bfa_sm_set_state(fcport, bfa_fcport_sm_linkup);

		WARN_ON(!fcport->event_cbfn);
		bfa_fcport_scn(fcport, BFA_PORT_LINKUP, BFA_FALSE);
		break;

	case BFA_FCPORT_SM_ENABLE:
		/*
		 * Already being enabled.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		if (bfa_fcport_send_disable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		else
			bfa_sm_set_state(fcport,
					 bfa_fcport_sm_disabling_qwait);

		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
				BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		wwn2str(pwwn_buf, fcport->pwwn);
		BFA_LOG(KERN_INFO, bfad, bfa_log_level,
			"Base port disabled: WWN = %s\n", pwwn_buf);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	case BFA_FCPORT_SM_FAA_MISCONFIG:
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		bfa_sm_set_state(fcport, bfa_fcport_sm_faa_misconfig);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_linkdown(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event)
{
	struct bfi_fcport_event_s *pevent = fcport->event_arg.i2hmsg.event;
	char pwwn_buf[BFA_STRING_32];
	struct bfad_s *bfad = (struct bfad_s *)fcport->bfa->bfad;

	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_LINKUP:
		bfa_fcport_update_linkinfo(fcport);
		bfa_sm_set_state(fcport, bfa_fcport_sm_linkup);
		WARN_ON(!fcport->event_cbfn);
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
				BFA_PL_EID_PORT_ST_CHANGE, 0, "Port Linkup");
		if (!bfa_ioc_get_fcmode(&fcport->bfa->ioc)) {

			bfa_trc(fcport->bfa,
				pevent->link_state.attr.vc_fcf.fcf.fipenabled);
			bfa_trc(fcport->bfa,
				pevent->link_state.attr.vc_fcf.fcf.fipfailed);

			if (pevent->link_state.attr.vc_fcf.fcf.fipfailed)
				bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
					BFA_PL_EID_FIP_FCF_DISC, 0,
					"FIP FCF Discovery Failed");
			else
				bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
					BFA_PL_EID_FIP_FCF_DISC, 0,
					"FIP FCF Discovered");
		}

		bfa_fcport_scn(fcport, BFA_PORT_LINKUP, BFA_FALSE);
		wwn2str(pwwn_buf, fcport->pwwn);
		BFA_LOG(KERN_INFO, bfad, bfa_log_level,
			"Base port online: WWN = %s\n", pwwn_buf);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_ONLINE);

		/* If QoS is enabled and it is not online, send AEN */
		if (fcport->cfg.qos_enabled &&
		    fcport->qos_attr.state != BFA_QOS_ONLINE)
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_QOS_NEG);
		break;

	case BFA_FCPORT_SM_LINKDOWN:
		/*
		 * Possible to get link down event.
		 */
		break;

	case BFA_FCPORT_SM_ENABLE:
		/*
		 * Already enabled.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		if (bfa_fcport_send_disable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		else
			bfa_sm_set_state(fcport,
					 bfa_fcport_sm_disabling_qwait);

		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
				BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		wwn2str(pwwn_buf, fcport->pwwn);
		BFA_LOG(KERN_INFO, bfad, bfa_log_level,
			"Base port disabled: WWN = %s\n", pwwn_buf);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	case BFA_FCPORT_SM_FAA_MISCONFIG:
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		bfa_sm_set_state(fcport, bfa_fcport_sm_faa_misconfig);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_linkup(struct bfa_fcport_s *fcport,
	enum bfa_fcport_sm_event event)
{
	char pwwn_buf[BFA_STRING_32];
	struct bfad_s *bfad = (struct bfad_s *)fcport->bfa->bfad;

	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_ENABLE:
		/*
		 * Already enabled.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		if (bfa_fcport_send_disable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		else
			bfa_sm_set_state(fcport,
					 bfa_fcport_sm_disabling_qwait);

		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_scn(fcport, BFA_PORT_LINKDOWN, BFA_FALSE);
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
				BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		wwn2str(pwwn_buf, fcport->pwwn);
		BFA_LOG(KERN_INFO, bfad, bfa_log_level,
			"Base port offline: WWN = %s\n", pwwn_buf);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_OFFLINE);
		BFA_LOG(KERN_INFO, bfad, bfa_log_level,
			"Base port disabled: WWN = %s\n", pwwn_buf);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_FCPORT_SM_LINKDOWN:
		bfa_sm_set_state(fcport, bfa_fcport_sm_linkdown);
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_scn(fcport, BFA_PORT_LINKDOWN, BFA_FALSE);
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
				BFA_PL_EID_PORT_ST_CHANGE, 0, "Port Linkdown");
		wwn2str(pwwn_buf, fcport->pwwn);
		if (BFA_PORT_IS_DISABLED(fcport->bfa)) {
			BFA_LOG(KERN_INFO, bfad, bfa_log_level,
				"Base port offline: WWN = %s\n", pwwn_buf);
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_OFFLINE);
		} else {
			BFA_LOG(KERN_ERR, bfad, bfa_log_level,
				"Base port (WWN = %s) "
				"lost fabric connectivity\n", pwwn_buf);
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		}
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		bfa_fcport_reset_linkinfo(fcport);
		wwn2str(pwwn_buf, fcport->pwwn);
		if (BFA_PORT_IS_DISABLED(fcport->bfa)) {
			BFA_LOG(KERN_INFO, bfad, bfa_log_level,
				"Base port offline: WWN = %s\n", pwwn_buf);
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_OFFLINE);
		} else {
			BFA_LOG(KERN_ERR, bfad, bfa_log_level,
				"Base port (WWN = %s) "
				"lost fabric connectivity\n", pwwn_buf);
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		}
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_scn(fcport, BFA_PORT_LINKDOWN, BFA_FALSE);
		wwn2str(pwwn_buf, fcport->pwwn);
		if (BFA_PORT_IS_DISABLED(fcport->bfa)) {
			BFA_LOG(KERN_INFO, bfad, bfa_log_level,
				"Base port offline: WWN = %s\n", pwwn_buf);
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_OFFLINE);
		} else {
			BFA_LOG(KERN_ERR, bfad, bfa_log_level,
				"Base port (WWN = %s) "
				"lost fabric connectivity\n", pwwn_buf);
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		}
		break;

	case BFA_FCPORT_SM_FAA_MISCONFIG:
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		bfa_sm_set_state(fcport, bfa_fcport_sm_faa_misconfig);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_disabling_qwait(struct bfa_fcport_s *fcport,
				 enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_QRESUME:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		bfa_fcport_send_disable(fcport);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		bfa_reqq_wcancel(&fcport->reqq_wait);
		break;

	case BFA_FCPORT_SM_ENABLE:
		bfa_sm_set_state(fcport, bfa_fcport_sm_toggling_qwait);
		break;

	case BFA_FCPORT_SM_DISABLE:
		/*
		 * Already being disabled.
		 */
		break;

	case BFA_FCPORT_SM_LINKUP:
	case BFA_FCPORT_SM_LINKDOWN:
		/*
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocfail);
		bfa_reqq_wcancel(&fcport->reqq_wait);
		break;

	case BFA_FCPORT_SM_FAA_MISCONFIG:
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		bfa_sm_set_state(fcport, bfa_fcport_sm_faa_misconfig);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_toggling_qwait(struct bfa_fcport_s *fcport,
				 enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_QRESUME:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		bfa_fcport_send_disable(fcport);
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport,
					 bfa_fcport_sm_enabling_qwait);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		bfa_reqq_wcancel(&fcport->reqq_wait);
		break;

	case BFA_FCPORT_SM_ENABLE:
		break;

	case BFA_FCPORT_SM_DISABLE:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabling_qwait);
		break;

	case BFA_FCPORT_SM_LINKUP:
	case BFA_FCPORT_SM_LINKDOWN:
		/*
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocfail);
		bfa_reqq_wcancel(&fcport->reqq_wait);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_disabling(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event)
{
	char pwwn_buf[BFA_STRING_32];
	struct bfad_s *bfad = (struct bfad_s *)fcport->bfa->bfad;
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_FWRSP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		break;

	case BFA_FCPORT_SM_DISABLE:
		/*
		 * Already being disabled.
		 */
		break;

	case BFA_FCPORT_SM_ENABLE:
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport,
					 bfa_fcport_sm_enabling_qwait);

		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
				BFA_PL_EID_PORT_ENABLE, 0, "Port Enable");
		wwn2str(pwwn_buf, fcport->pwwn);
		BFA_LOG(KERN_INFO, bfad, bfa_log_level,
			"Base port enabled: WWN = %s\n", pwwn_buf);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_ENABLE);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_LINKUP:
	case BFA_FCPORT_SM_LINKDOWN:
		/*
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocfail);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_disabled(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event)
{
	char pwwn_buf[BFA_STRING_32];
	struct bfad_s *bfad = (struct bfad_s *)fcport->bfa->bfad;
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		/*
		 * Ignore start event for a port that is disabled.
		 */
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_ENABLE:
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport,
					 bfa_fcport_sm_enabling_qwait);

		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
				BFA_PL_EID_PORT_ENABLE, 0, "Port Enable");
		wwn2str(pwwn_buf, fcport->pwwn);
		BFA_LOG(KERN_INFO, bfad, bfa_log_level,
			"Base port enabled: WWN = %s\n", pwwn_buf);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_ENABLE);
		break;

	case BFA_FCPORT_SM_DISABLE:
		/*
		 * Already disabled.
		 */
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocfail);
		break;

	case BFA_FCPORT_SM_DPORTENABLE:
		bfa_sm_set_state(fcport, bfa_fcport_sm_dport);
		break;

	case BFA_FCPORT_SM_DDPORTENABLE:
		bfa_sm_set_state(fcport, bfa_fcport_sm_ddport);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_stopped(struct bfa_fcport_s *fcport,
			 enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport,
					 bfa_fcport_sm_enabling_qwait);
		break;

	default:
		/*
		 * Ignore all other events.
		 */
		;
	}
}

/*
 * Port is enabled. IOC is down/failed.
 */
static void
bfa_fcport_sm_iocdown(struct bfa_fcport_s *fcport,
			 enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport,
					 bfa_fcport_sm_enabling_qwait);
		break;

	default:
		/*
		 * Ignore all events.
		 */
		;
	}
}

/*
 * Port is disabled. IOC is down/failed.
 */
static void
bfa_fcport_sm_iocfail(struct bfa_fcport_s *fcport,
			 enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		break;

	case BFA_FCPORT_SM_ENABLE:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	default:
		/*
		 * Ignore all events.
		 */
		;
	}
}

static void
bfa_fcport_sm_dport(struct bfa_fcport_s *fcport, enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_DPORTENABLE:
	case BFA_FCPORT_SM_DISABLE:
	case BFA_FCPORT_SM_ENABLE:
	case BFA_FCPORT_SM_START:
		/*
		 * Ignore event for a port that is dport
		 */
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocfail);
		break;

	case BFA_FCPORT_SM_DPORTDISABLE:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_ddport(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_DISABLE:
	case BFA_FCPORT_SM_DDPORTDISABLE:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		break;

	case BFA_FCPORT_SM_DPORTENABLE:
	case BFA_FCPORT_SM_DPORTDISABLE:
	case BFA_FCPORT_SM_ENABLE:
	case BFA_FCPORT_SM_START:
		/**
		 * Ignore event for a port that is ddport
		 */
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocfail);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_faa_misconfig(struct bfa_fcport_s *fcport,
			    enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_DPORTENABLE:
	case BFA_FCPORT_SM_ENABLE:
	case BFA_FCPORT_SM_START:
		/*
		 * Ignore event for a port as there is FAA misconfig
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		if (bfa_fcport_send_disable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		else
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling_qwait);

		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_scn(fcport, BFA_PORT_LINKDOWN, BFA_FALSE);
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_scn(fcport, BFA_PORT_LINKDOWN, BFA_FALSE);
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

/*
 * Link state is down
 */
static void
bfa_fcport_ln_sm_dn(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKUP:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_nf);
		bfa_fcport_queue_cb(ln, BFA_PORT_LINKUP);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/*
 * Link state is waiting for down notification
 */
static void
bfa_fcport_ln_sm_dn_nf(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKUP:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_up_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/*
 * Link state is waiting for down notification and there is a pending up
 */
static void
bfa_fcport_ln_sm_dn_up_nf(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKDOWN:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_nf);
		bfa_fcport_queue_cb(ln, BFA_PORT_LINKUP);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/*
 * Link state is up
 */
static void
bfa_fcport_ln_sm_up(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKDOWN:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_nf);
		bfa_fcport_queue_cb(ln, BFA_PORT_LINKDOWN);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/*
 * Link state is waiting for up notification
 */
static void
bfa_fcport_ln_sm_up_nf(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKDOWN:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_dn_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/*
 * Link state is waiting for up notification and there is a pending down
 */
static void
bfa_fcport_ln_sm_up_dn_nf(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKUP:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_dn_up_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_nf);
		bfa_fcport_queue_cb(ln, BFA_PORT_LINKDOWN);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/*
 * Link state is waiting for up notification and there are pending down and up
 */
static void
bfa_fcport_ln_sm_up_dn_up_nf(struct bfa_fcport_ln_s *ln,
			enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKDOWN:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_dn_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_up_nf);
		bfa_fcport_queue_cb(ln, BFA_PORT_LINKDOWN);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

static void
__bfa_cb_fcport_event(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_fcport_ln_s *ln = cbarg;

	if (complete)
		ln->fcport->event_cbfn(ln->fcport->event_cbarg, ln->ln_event);
	else
		bfa_sm_send_event(ln, BFA_FCPORT_LN_SM_NOTIFICATION);
}

/*
 * Send SCN notification to upper layers.
 * trunk - false if caller is fcport to ignore fcport event in trunked mode
 */
static void
bfa_fcport_scn(struct bfa_fcport_s *fcport, enum bfa_port_linkstate event,
	bfa_boolean_t trunk)
{
	if (fcport->cfg.trunked && !trunk)
		return;

	switch (event) {
	case BFA_PORT_LINKUP:
		bfa_sm_send_event(&fcport->ln, BFA_FCPORT_LN_SM_LINKUP);
		break;
	case BFA_PORT_LINKDOWN:
		bfa_sm_send_event(&fcport->ln, BFA_FCPORT_LN_SM_LINKDOWN);
		break;
	default:
		WARN_ON(1);
	}
}

static void
bfa_fcport_queue_cb(struct bfa_fcport_ln_s *ln, enum bfa_port_linkstate event)
{
	struct bfa_fcport_s *fcport = ln->fcport;

	if (fcport->bfa->fcs) {
		fcport->event_cbfn(fcport->event_cbarg, event);
		bfa_sm_send_event(ln, BFA_FCPORT_LN_SM_NOTIFICATION);
	} else {
		ln->ln_event = event;
		bfa_cb_queue(fcport->bfa, &ln->ln_qe,
			__bfa_cb_fcport_event, ln);
	}
}

#define FCPORT_STATS_DMA_SZ (BFA_ROUNDUP(sizeof(union bfa_fcport_stats_u), \
							BFA_CACHELINE_SZ))

void
bfa_fcport_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *minfo,
		   struct bfa_s *bfa)
{
	struct bfa_mem_dma_s *fcport_dma = BFA_MEM_FCPORT_DMA(bfa);

	bfa_mem_dma_setup(minfo, fcport_dma, FCPORT_STATS_DMA_SZ);
}

static void
bfa_fcport_qresume(void *cbarg)
{
	struct bfa_fcport_s *fcport = cbarg;

	bfa_sm_send_event(fcport, BFA_FCPORT_SM_QRESUME);
}

static void
bfa_fcport_mem_claim(struct bfa_fcport_s *fcport)
{
	struct bfa_mem_dma_s *fcport_dma = &fcport->fcport_dma;

	fcport->stats_kva = bfa_mem_dma_virt(fcport_dma);
	fcport->stats_pa  = bfa_mem_dma_phys(fcport_dma);
	fcport->stats = (union bfa_fcport_stats_u *)
				bfa_mem_dma_virt(fcport_dma);
}

/*
 * Memory initialization.
 */
void
bfa_fcport_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_pcidev_s *pcidev)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	struct bfa_port_cfg_s *port_cfg = &fcport->cfg;
	struct bfa_fcport_ln_s *ln = &fcport->ln;

	fcport->bfa = bfa;
	ln->fcport = fcport;

	bfa_fcport_mem_claim(fcport);

	bfa_sm_set_state(fcport, bfa_fcport_sm_uninit);
	bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn);

	/*
	 * initialize time stamp for stats reset
	 */
	fcport->stats_reset_time = ktime_get_seconds();
	fcport->stats_dma_ready = BFA_FALSE;

	/*
	 * initialize and set default configuration
	 */
	port_cfg->topology = BFA_PORT_TOPOLOGY_P2P;
	port_cfg->speed = BFA_PORT_SPEED_AUTO;
	port_cfg->trunked = BFA_FALSE;
	port_cfg->maxfrsize = 0;

	port_cfg->trl_def_speed = BFA_PORT_SPEED_1GBPS;
	port_cfg->qos_bw.high = BFA_QOS_BW_HIGH;
	port_cfg->qos_bw.med = BFA_QOS_BW_MED;
	port_cfg->qos_bw.low = BFA_QOS_BW_LOW;

	fcport->fec_state = BFA_FEC_OFFLINE;

	INIT_LIST_HEAD(&fcport->stats_pending_q);
	INIT_LIST_HEAD(&fcport->statsclr_pending_q);

	bfa_reqq_winit(&fcport->reqq_wait, bfa_fcport_qresume, fcport);
}

void
bfa_fcport_start(struct bfa_s *bfa)
{
	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_START);
}

/*
 * Called when IOC failure is detected.
 */
void
bfa_fcport_iocdisable(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_sm_send_event(fcport, BFA_FCPORT_SM_HWFAIL);
	bfa_trunk_iocdisable(bfa);
}

/*
 * Update loop info in fcport for SCN online
 */
static void
bfa_fcport_update_loop_info(struct bfa_fcport_s *fcport,
			struct bfa_fcport_loop_info_s *loop_info)
{
	fcport->myalpa = loop_info->myalpa;
	fcport->alpabm_valid =
			loop_info->alpabm_val;
	memcpy(fcport->alpabm.alpa_bm,
			loop_info->alpabm.alpa_bm,
			sizeof(struct fc_alpabm_s));
}

static void
bfa_fcport_update_linkinfo(struct bfa_fcport_s *fcport)
{
	struct bfi_fcport_event_s *pevent = fcport->event_arg.i2hmsg.event;
	struct bfa_fcport_trunk_s *trunk = &fcport->trunk;

	fcport->speed = pevent->link_state.speed;
	fcport->topology = pevent->link_state.topology;

	if (fcport->topology == BFA_PORT_TOPOLOGY_LOOP) {
		bfa_fcport_update_loop_info(fcport,
				&pevent->link_state.attr.loop_info);
		return;
	}

	/* QoS Details */
	fcport->qos_attr = pevent->link_state.qos_attr;
	fcport->qos_vc_attr = pevent->link_state.attr.vc_fcf.qos_vc_attr;

	if (fcport->cfg.bb_cr_enabled)
		fcport->bbcr_attr = pevent->link_state.attr.bbcr_attr;

	fcport->fec_state = pevent->link_state.fec_state;

	/*
	 * update trunk state if applicable
	 */
	if (!fcport->cfg.trunked)
		trunk->attr.state = BFA_TRUNK_DISABLED;

	/* update FCoE specific */
	fcport->fcoe_vlan =
		be16_to_cpu(pevent->link_state.attr.vc_fcf.fcf.vlan);

	bfa_trc(fcport->bfa, fcport->speed);
	bfa_trc(fcport->bfa, fcport->topology);
}

static void
bfa_fcport_reset_linkinfo(struct bfa_fcport_s *fcport)
{
	fcport->speed = BFA_PORT_SPEED_UNKNOWN;
	fcport->topology = BFA_PORT_TOPOLOGY_NONE;
	fcport->fec_state = BFA_FEC_OFFLINE;
}

/*
 * Send port enable message to firmware.
 */
static bfa_boolean_t
bfa_fcport_send_enable(struct bfa_fcport_s *fcport)
{
	struct bfi_fcport_enable_req_s *m;

	/*
	 * Increment message tag before queue check, so that responses to old
	 * requests are discarded.
	 */
	fcport->msgtag++;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(fcport->bfa, BFA_REQQ_PORT);
	if (!m) {
		bfa_reqq_wait(fcport->bfa, BFA_REQQ_PORT,
							&fcport->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_FCPORT, BFI_FCPORT_H2I_ENABLE_REQ,
			bfa_fn_lpu(fcport->bfa));
	m->nwwn = fcport->nwwn;
	m->pwwn = fcport->pwwn;
	m->port_cfg = fcport->cfg;
	m->msgtag = fcport->msgtag;
	m->port_cfg.maxfrsize = cpu_to_be16(fcport->cfg.maxfrsize);
	 m->use_flash_cfg = fcport->use_flash_cfg;
	bfa_dma_be_addr_set(m->stats_dma_addr, fcport->stats_pa);
	bfa_trc(fcport->bfa, m->stats_dma_addr.a32.addr_lo);
	bfa_trc(fcport->bfa, m->stats_dma_addr.a32.addr_hi);

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(fcport->bfa, BFA_REQQ_PORT, m->mh);
	return BFA_TRUE;
}

/*
 * Send port disable message to firmware.
 */
static	bfa_boolean_t
bfa_fcport_send_disable(struct bfa_fcport_s *fcport)
{
	struct bfi_fcport_req_s *m;

	/*
	 * Increment message tag before queue check, so that responses to old
	 * requests are discarded.
	 */
	fcport->msgtag++;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(fcport->bfa, BFA_REQQ_PORT);
	if (!m) {
		bfa_reqq_wait(fcport->bfa, BFA_REQQ_PORT,
							&fcport->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_FCPORT, BFI_FCPORT_H2I_DISABLE_REQ,
			bfa_fn_lpu(fcport->bfa));
	m->msgtag = fcport->msgtag;

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(fcport->bfa, BFA_REQQ_PORT, m->mh);

	return BFA_TRUE;
}

static void
bfa_fcport_set_wwns(struct bfa_fcport_s *fcport)
{
	fcport->pwwn = fcport->bfa->ioc.attr->pwwn;
	fcport->nwwn = fcport->bfa->ioc.attr->nwwn;

	bfa_trc(fcport->bfa, fcport->pwwn);
	bfa_trc(fcport->bfa, fcport->nwwn);
}

static void
bfa_fcport_qos_stats_swap(struct bfa_qos_stats_s *d,
	struct bfa_qos_stats_s *s)
{
	u32	*dip = (u32 *) d;
	__be32	*sip = (__be32 *) s;
	int		i;

	/* Now swap the 32 bit fields */
	for (i = 0; i < (sizeof(struct bfa_qos_stats_s)/sizeof(u32)); ++i)
		dip[i] = be32_to_cpu(sip[i]);
}

static void
bfa_fcport_fcoe_stats_swap(struct bfa_fcoe_stats_s *d,
	struct bfa_fcoe_stats_s *s)
{
	u32	*dip = (u32 *) d;
	__be32	*sip = (__be32 *) s;
	int		i;

	for (i = 0; i < ((sizeof(struct bfa_fcoe_stats_s))/sizeof(u32));
	     i = i + 2) {
#ifdef __BIG_ENDIAN
		dip[i] = be32_to_cpu(sip[i]);
		dip[i + 1] = be32_to_cpu(sip[i + 1]);
#else
		dip[i] = be32_to_cpu(sip[i + 1]);
		dip[i + 1] = be32_to_cpu(sip[i]);
#endif
	}
}

static void
__bfa_cb_fcport_stats_get(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *)cbarg;
	struct bfa_cb_pending_q_s *cb;
	struct list_head *qe, *qen;
	union bfa_fcport_stats_u *ret;

	if (complete) {
		time64_t time = ktime_get_seconds();

		list_for_each_safe(qe, qen, &fcport->stats_pending_q) {
			bfa_q_deq(&fcport->stats_pending_q, &qe);
			cb = (struct bfa_cb_pending_q_s *)qe;
			if (fcport->stats_status == BFA_STATUS_OK) {
				ret = (union bfa_fcport_stats_u *)cb->data;
				/* Swap FC QoS or FCoE stats */
				if (bfa_ioc_get_fcmode(&fcport->bfa->ioc))
					bfa_fcport_qos_stats_swap(&ret->fcqos,
							&fcport->stats->fcqos);
				else {
					bfa_fcport_fcoe_stats_swap(&ret->fcoe,
							&fcport->stats->fcoe);
					ret->fcoe.secs_reset =
						time - fcport->stats_reset_time;
				}
			}
			bfa_cb_queue_status(fcport->bfa, &cb->hcb_qe,
					fcport->stats_status);
		}
		fcport->stats_status = BFA_STATUS_OK;
	} else {
		INIT_LIST_HEAD(&fcport->stats_pending_q);
		fcport->stats_status = BFA_STATUS_OK;
	}
}

static void
bfa_fcport_stats_get_timeout(void *cbarg)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *) cbarg;

	bfa_trc(fcport->bfa, fcport->stats_qfull);

	if (fcport->stats_qfull) {
		bfa_reqq_wcancel(&fcport->stats_reqq_wait);
		fcport->stats_qfull = BFA_FALSE;
	}

	fcport->stats_status = BFA_STATUS_ETIMER;
	__bfa_cb_fcport_stats_get(fcport, BFA_TRUE);
}

static void
bfa_fcport_send_stats_get(void *cbarg)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *) cbarg;
	struct bfi_fcport_req_s *msg;

	msg = bfa_reqq_next(fcport->bfa, BFA_REQQ_PORT);

	if (!msg) {
		fcport->stats_qfull = BFA_TRUE;
		bfa_reqq_winit(&fcport->stats_reqq_wait,
				bfa_fcport_send_stats_get, fcport);
		bfa_reqq_wait(fcport->bfa, BFA_REQQ_PORT,
				&fcport->stats_reqq_wait);
		return;
	}
	fcport->stats_qfull = BFA_FALSE;

	memset(msg, 0, sizeof(struct bfi_fcport_req_s));
	bfi_h2i_set(msg->mh, BFI_MC_FCPORT, BFI_FCPORT_H2I_STATS_GET_REQ,
			bfa_fn_lpu(fcport->bfa));
	bfa_reqq_produce(fcport->bfa, BFA_REQQ_PORT, msg->mh);
}

static void
__bfa_cb_fcport_stats_clr(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *) cbarg;
	struct bfa_cb_pending_q_s *cb;
	struct list_head *qe, *qen;

	if (complete) {
		/*
		 * re-initialize time stamp for stats reset
		 */
		fcport->stats_reset_time = ktime_get_seconds();
		list_for_each_safe(qe, qen, &fcport->statsclr_pending_q) {
			bfa_q_deq(&fcport->statsclr_pending_q, &qe);
			cb = (struct bfa_cb_pending_q_s *)qe;
			bfa_cb_queue_status(fcport->bfa, &cb->hcb_qe,
						fcport->stats_status);
		}
		fcport->stats_status = BFA_STATUS_OK;
	} else {
		INIT_LIST_HEAD(&fcport->statsclr_pending_q);
		fcport->stats_status = BFA_STATUS_OK;
	}
}

static void
bfa_fcport_stats_clr_timeout(void *cbarg)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *) cbarg;

	bfa_trc(fcport->bfa, fcport->stats_qfull);

	if (fcport->stats_qfull) {
		bfa_reqq_wcancel(&fcport->stats_reqq_wait);
		fcport->stats_qfull = BFA_FALSE;
	}

	fcport->stats_status = BFA_STATUS_ETIMER;
	__bfa_cb_fcport_stats_clr(fcport, BFA_TRUE);
}

static void
bfa_fcport_send_stats_clear(void *cbarg)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *) cbarg;
	struct bfi_fcport_req_s *msg;

	msg = bfa_reqq_next(fcport->bfa, BFA_REQQ_PORT);

	if (!msg) {
		fcport->stats_qfull = BFA_TRUE;
		bfa_reqq_winit(&fcport->stats_reqq_wait,
				bfa_fcport_send_stats_clear, fcport);
		bfa_reqq_wait(fcport->bfa, BFA_REQQ_PORT,
						&fcport->stats_reqq_wait);
		return;
	}
	fcport->stats_qfull = BFA_FALSE;

	memset(msg, 0, sizeof(struct bfi_fcport_req_s));
	bfi_h2i_set(msg->mh, BFI_MC_FCPORT, BFI_FCPORT_H2I_STATS_CLEAR_REQ,
			bfa_fn_lpu(fcport->bfa));
	bfa_reqq_produce(fcport->bfa, BFA_REQQ_PORT, msg->mh);
}

/*
 * Handle trunk SCN event from firmware.
 */
static void
bfa_trunk_scn(struct bfa_fcport_s *fcport, struct bfi_fcport_trunk_scn_s *scn)
{
	struct bfa_fcport_trunk_s *trunk = &fcport->trunk;
	struct bfi_fcport_trunk_link_s *tlink;
	struct bfa_trunk_link_attr_s *lattr;
	enum bfa_trunk_state state_prev;
	int i;
	int link_bm = 0;

	bfa_trc(fcport->bfa, fcport->cfg.trunked);
	WARN_ON(scn->trunk_state != BFA_TRUNK_ONLINE &&
		   scn->trunk_state != BFA_TRUNK_OFFLINE);

	bfa_trc(fcport->bfa, trunk->attr.state);
	bfa_trc(fcport->bfa, scn->trunk_state);
	bfa_trc(fcport->bfa, scn->trunk_speed);

	/*
	 * Save off new state for trunk attribute query
	 */
	state_prev = trunk->attr.state;
	if (fcport->cfg.trunked && (trunk->attr.state != BFA_TRUNK_DISABLED))
		trunk->attr.state = scn->trunk_state;
	trunk->attr.speed = scn->trunk_speed;
	for (i = 0; i < BFA_TRUNK_MAX_PORTS; i++) {
		lattr = &trunk->attr.link_attr[i];
		tlink = &scn->tlink[i];

		lattr->link_state = tlink->state;
		lattr->trunk_wwn  = tlink->trunk_wwn;
		lattr->fctl	  = tlink->fctl;
		lattr->speed	  = tlink->speed;
		lattr->deskew	  = be32_to_cpu(tlink->deskew);

		if (tlink->state == BFA_TRUNK_LINK_STATE_UP) {
			fcport->speed	 = tlink->speed;
			fcport->topology = BFA_PORT_TOPOLOGY_P2P;
			link_bm |= 1 << i;
		}

		bfa_trc(fcport->bfa, lattr->link_state);
		bfa_trc(fcport->bfa, lattr->trunk_wwn);
		bfa_trc(fcport->bfa, lattr->fctl);
		bfa_trc(fcport->bfa, lattr->speed);
		bfa_trc(fcport->bfa, lattr->deskew);
	}

	switch (link_bm) {
	case 3:
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			BFA_PL_EID_TRUNK_SCN, 0, "Trunk up(0,1)");
		break;
	case 2:
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			BFA_PL_EID_TRUNK_SCN, 0, "Trunk up(-,1)");
		break;
	case 1:
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			BFA_PL_EID_TRUNK_SCN, 0, "Trunk up(0,-)");
		break;
	default:
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			BFA_PL_EID_TRUNK_SCN, 0, "Trunk down");
	}

	/*
	 * Notify upper layers if trunk state changed.
	 */
	if ((state_prev != trunk->attr.state) ||
		(scn->trunk_state == BFA_TRUNK_OFFLINE)) {
		bfa_fcport_scn(fcport, (scn->trunk_state == BFA_TRUNK_ONLINE) ?
			BFA_PORT_LINKUP : BFA_PORT_LINKDOWN, BFA_TRUE);
	}
}

static void
bfa_trunk_iocdisable(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	int i = 0;

	/*
	 * In trunked mode, notify upper layers that link is down
	 */
	if (fcport->cfg.trunked) {
		if (fcport->trunk.attr.state == BFA_TRUNK_ONLINE)
			bfa_fcport_scn(fcport, BFA_PORT_LINKDOWN, BFA_TRUE);

		fcport->trunk.attr.state = BFA_TRUNK_OFFLINE;
		fcport->trunk.attr.speed = BFA_PORT_SPEED_UNKNOWN;
		for (i = 0; i < BFA_TRUNK_MAX_PORTS; i++) {
			fcport->trunk.attr.link_attr[i].trunk_wwn = 0;
			fcport->trunk.attr.link_attr[i].fctl =
						BFA_TRUNK_LINK_FCTL_NORMAL;
			fcport->trunk.attr.link_attr[i].link_state =
						BFA_TRUNK_LINK_STATE_DN_LINKDN;
			fcport->trunk.attr.link_attr[i].speed =
						BFA_PORT_SPEED_UNKNOWN;
			fcport->trunk.attr.link_attr[i].deskew = 0;
		}
	}
}

/*
 * Called to initialize port attributes
 */
void
bfa_fcport_init(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	/*
	 * Initialize port attributes from IOC hardware data.
	 */
	bfa_fcport_set_wwns(fcport);
	if (fcport->cfg.maxfrsize == 0)
		fcport->cfg.maxfrsize = bfa_ioc_maxfrsize(&bfa->ioc);
	fcport->cfg.rx_bbcredit = bfa_ioc_rx_bbcredit(&bfa->ioc);
	fcport->speed_sup = bfa_ioc_speed_sup(&bfa->ioc);

	if (bfa_fcport_is_pbcdisabled(bfa))
		bfa->modules.port.pbc_disabled = BFA_TRUE;

	WARN_ON(!fcport->cfg.maxfrsize);
	WARN_ON(!fcport->cfg.rx_bbcredit);
	WARN_ON(!fcport->speed_sup);
}

/*
 * Firmware message handler.
 */
void
bfa_fcport_isr(struct bfa_s *bfa, struct bfi_msg_s *msg)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	union bfi_fcport_i2h_msg_u i2hmsg;

	i2hmsg.msg = msg;
	fcport->event_arg.i2hmsg = i2hmsg;

	bfa_trc(bfa, msg->mhdr.msg_id);
	bfa_trc(bfa, bfa_sm_to_state(hal_port_sm_table, fcport->sm));

	switch (msg->mhdr.msg_id) {
	case BFI_FCPORT_I2H_ENABLE_RSP:
		if (fcport->msgtag == i2hmsg.penable_rsp->msgtag) {

			fcport->stats_dma_ready = BFA_TRUE;
			if (fcport->use_flash_cfg) {
				fcport->cfg = i2hmsg.penable_rsp->port_cfg;
				fcport->cfg.maxfrsize =
					cpu_to_be16(fcport->cfg.maxfrsize);
				fcport->cfg.path_tov =
					cpu_to_be16(fcport->cfg.path_tov);
				fcport->cfg.q_depth =
					cpu_to_be16(fcport->cfg.q_depth);

				if (fcport->cfg.trunked)
					fcport->trunk.attr.state =
						BFA_TRUNK_OFFLINE;
				else
					fcport->trunk.attr.state =
						BFA_TRUNK_DISABLED;
				fcport->qos_attr.qos_bw =
					i2hmsg.penable_rsp->port_cfg.qos_bw;
				fcport->use_flash_cfg = BFA_FALSE;
			}

			if (fcport->cfg.qos_enabled)
				fcport->qos_attr.state = BFA_QOS_OFFLINE;
			else
				fcport->qos_attr.state = BFA_QOS_DISABLED;

			fcport->qos_attr.qos_bw_op =
					i2hmsg.penable_rsp->port_cfg.qos_bw;

			if (fcport->cfg.bb_cr_enabled)
				fcport->bbcr_attr.state = BFA_BBCR_OFFLINE;
			else
				fcport->bbcr_attr.state = BFA_BBCR_DISABLED;

			bfa_sm_send_event(fcport, BFA_FCPORT_SM_FWRSP);
		}
		break;

	case BFI_FCPORT_I2H_DISABLE_RSP:
		if (fcport->msgtag == i2hmsg.penable_rsp->msgtag)
			bfa_sm_send_event(fcport, BFA_FCPORT_SM_FWRSP);
		break;

	case BFI_FCPORT_I2H_EVENT:
		if (fcport->cfg.bb_cr_enabled)
			fcport->bbcr_attr.state = BFA_BBCR_OFFLINE;
		else
			fcport->bbcr_attr.state = BFA_BBCR_DISABLED;

		if (i2hmsg.event->link_state.linkstate == BFA_PORT_LINKUP)
			bfa_sm_send_event(fcport, BFA_FCPORT_SM_LINKUP);
		else {
			if (i2hmsg.event->link_state.linkstate_rsn ==
			    BFA_PORT_LINKSTATE_RSN_FAA_MISCONFIG)
				bfa_sm_send_event(fcport,
						  BFA_FCPORT_SM_FAA_MISCONFIG);
			else
				bfa_sm_send_event(fcport,
						  BFA_FCPORT_SM_LINKDOWN);
		}
		fcport->qos_attr.qos_bw_op =
				i2hmsg.event->link_state.qos_attr.qos_bw_op;
		break;

	case BFI_FCPORT_I2H_TRUNK_SCN:
		bfa_trunk_scn(fcport, i2hmsg.trunk_scn);
		break;

	case BFI_FCPORT_I2H_STATS_GET_RSP:
		/*
		 * check for timer pop before processing the rsp
		 */
		if (list_empty(&fcport->stats_pending_q) ||
		    (fcport->stats_status == BFA_STATUS_ETIMER))
			break;

		bfa_timer_stop(&fcport->timer);
		fcport->stats_status = i2hmsg.pstatsget_rsp->status;
		__bfa_cb_fcport_stats_get(fcport, BFA_TRUE);
		break;

	case BFI_FCPORT_I2H_STATS_CLEAR_RSP:
		/*
		 * check for timer pop before processing the rsp
		 */
		if (list_empty(&fcport->statsclr_pending_q) ||
		    (fcport->stats_status == BFA_STATUS_ETIMER))
			break;

		bfa_timer_stop(&fcport->timer);
		fcport->stats_status = BFA_STATUS_OK;
		__bfa_cb_fcport_stats_clr(fcport, BFA_TRUE);
		break;

	case BFI_FCPORT_I2H_ENABLE_AEN:
		bfa_sm_send_event(fcport, BFA_FCPORT_SM_ENABLE);
		break;

	case BFI_FCPORT_I2H_DISABLE_AEN:
		bfa_sm_send_event(fcport, BFA_FCPORT_SM_DISABLE);
		break;

	default:
		WARN_ON(1);
	break;
	}
}

/*
 * Registered callback for port events.
 */
void
bfa_fcport_event_register(struct bfa_s *bfa,
				void (*cbfn) (void *cbarg,
				enum bfa_port_linkstate event),
				void *cbarg)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	fcport->event_cbfn = cbfn;
	fcport->event_cbarg = cbarg;
}

bfa_status_t
bfa_fcport_enable(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	if (bfa_fcport_is_pbcdisabled(bfa))
		return BFA_STATUS_PBC;

	if (bfa_ioc_is_disabled(&bfa->ioc))
		return BFA_STATUS_IOC_DISABLED;

	if (fcport->diag_busy)
		return BFA_STATUS_DIAG_BUSY;

	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_ENABLE);
	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcport_disable(struct bfa_s *bfa)
{
	if (bfa_fcport_is_pbcdisabled(bfa))
		return BFA_STATUS_PBC;

	if (bfa_ioc_is_disabled(&bfa->ioc))
		return BFA_STATUS_IOC_DISABLED;

	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_DISABLE);
	return BFA_STATUS_OK;
}

/* If PBC is disabled on port, return error */
bfa_status_t
bfa_fcport_is_pbcdisabled(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	struct bfa_iocfc_s *iocfc = &bfa->iocfc;
	struct bfi_iocfc_cfgrsp_s *cfgrsp = iocfc->cfgrsp;

	if (cfgrsp->pbc_cfg.port_enabled == BFI_PBC_PORT_DISABLED) {
		bfa_trc(bfa, fcport->pwwn);
		return BFA_STATUS_PBC;
	}
	return BFA_STATUS_OK;
}

/*
 * Configure port speed.
 */
bfa_status_t
bfa_fcport_cfg_speed(struct bfa_s *bfa, enum bfa_port_speed speed)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, speed);

	if (fcport->cfg.trunked == BFA_TRUE)
		return BFA_STATUS_TRUNK_ENABLED;
	if ((fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP) &&
			(speed == BFA_PORT_SPEED_16GBPS))
		return BFA_STATUS_UNSUPP_SPEED;
	if ((speed != BFA_PORT_SPEED_AUTO) && (speed > fcport->speed_sup)) {
		bfa_trc(bfa, fcport->speed_sup);
		return BFA_STATUS_UNSUPP_SPEED;
	}

	/* Port speed entered needs to be checked */
	if (bfa_ioc_get_type(&fcport->bfa->ioc) == BFA_IOC_TYPE_FC) {
		/* For CT2, 1G is not supported */
		if ((speed == BFA_PORT_SPEED_1GBPS) &&
		    (bfa_asic_id_ct2(bfa->ioc.pcidev.device_id)))
			return BFA_STATUS_UNSUPP_SPEED;

		/* Already checked for Auto Speed and Max Speed supp */
		if (!(speed == BFA_PORT_SPEED_1GBPS ||
		      speed == BFA_PORT_SPEED_2GBPS ||
		      speed == BFA_PORT_SPEED_4GBPS ||
		      speed == BFA_PORT_SPEED_8GBPS ||
		      speed == BFA_PORT_SPEED_16GBPS ||
		      speed == BFA_PORT_SPEED_AUTO))
			return BFA_STATUS_UNSUPP_SPEED;
	} else {
		if (speed != BFA_PORT_SPEED_10GBPS)
			return BFA_STATUS_UNSUPP_SPEED;
	}

	fcport->cfg.speed = speed;

	return BFA_STATUS_OK;
}

/*
 * Get current speed.
 */
enum bfa_port_speed
bfa_fcport_get_speed(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->speed;
}

/*
 * Configure port topology.
 */
bfa_status_t
bfa_fcport_cfg_topology(struct bfa_s *bfa, enum bfa_port_topology topology)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, topology);
	bfa_trc(bfa, fcport->cfg.topology);

	switch (topology) {
	case BFA_PORT_TOPOLOGY_P2P:
		break;

	case BFA_PORT_TOPOLOGY_LOOP:
		if ((bfa_fcport_is_qos_enabled(bfa) != BFA_FALSE) ||
			(fcport->qos_attr.state != BFA_QOS_DISABLED))
			return BFA_STATUS_ERROR_QOS_ENABLED;
		if (fcport->cfg.ratelimit != BFA_FALSE)
			return BFA_STATUS_ERROR_TRL_ENABLED;
		if ((bfa_fcport_is_trunk_enabled(bfa) != BFA_FALSE) ||
			(fcport->trunk.attr.state != BFA_TRUNK_DISABLED))
			return BFA_STATUS_ERROR_TRUNK_ENABLED;
		if ((bfa_fcport_get_speed(bfa) == BFA_PORT_SPEED_16GBPS) ||
			(fcport->cfg.speed == BFA_PORT_SPEED_16GBPS))
			return BFA_STATUS_UNSUPP_SPEED;
		if (bfa_mfg_is_mezz(bfa->ioc.attr->card_type))
			return BFA_STATUS_LOOP_UNSUPP_MEZZ;
		if (bfa_fcport_is_dport(bfa) != BFA_FALSE)
			return BFA_STATUS_DPORT_ERR;
		if (bfa_fcport_is_ddport(bfa) != BFA_FALSE)
			return BFA_STATUS_DPORT_ERR;
		break;

	case BFA_PORT_TOPOLOGY_AUTO:
		break;

	default:
		return BFA_STATUS_EINVAL;
	}

	fcport->cfg.topology = topology;
	return BFA_STATUS_OK;
}

/*
 * Get current topology.
 */
enum bfa_port_topology
bfa_fcport_get_topology(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->topology;
}

/**
 * Get config topology.
 */
enum bfa_port_topology
bfa_fcport_get_cfg_topology(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->cfg.topology;
}

bfa_status_t
bfa_fcport_cfg_hardalpa(struct bfa_s *bfa, u8 alpa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, alpa);
	bfa_trc(bfa, fcport->cfg.cfg_hardalpa);
	bfa_trc(bfa, fcport->cfg.hardalpa);

	fcport->cfg.cfg_hardalpa = BFA_TRUE;
	fcport->cfg.hardalpa = alpa;

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcport_clr_hardalpa(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, fcport->cfg.cfg_hardalpa);
	bfa_trc(bfa, fcport->cfg.hardalpa);

	fcport->cfg.cfg_hardalpa = BFA_FALSE;
	return BFA_STATUS_OK;
}

bfa_boolean_t
bfa_fcport_get_hardalpa(struct bfa_s *bfa, u8 *alpa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	*alpa = fcport->cfg.hardalpa;
	return fcport->cfg.cfg_hardalpa;
}

u8
bfa_fcport_get_myalpa(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->myalpa;
}

bfa_status_t
bfa_fcport_cfg_maxfrsize(struct bfa_s *bfa, u16 maxfrsize)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, maxfrsize);
	bfa_trc(bfa, fcport->cfg.maxfrsize);

	/* with in range */
	if ((maxfrsize > FC_MAX_PDUSZ) || (maxfrsize < FC_MIN_PDUSZ))
		return BFA_STATUS_INVLD_DFSZ;

	/* power of 2, if not the max frame size of 2112 */
	if ((maxfrsize != FC_MAX_PDUSZ) && (maxfrsize & (maxfrsize - 1)))
		return BFA_STATUS_INVLD_DFSZ;

	fcport->cfg.maxfrsize = maxfrsize;
	return BFA_STATUS_OK;
}

u16
bfa_fcport_get_maxfrsize(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->cfg.maxfrsize;
}

u8
bfa_fcport_get_rx_bbcredit(struct bfa_s *bfa)
{
	if (bfa_fcport_get_topology(bfa) != BFA_PORT_TOPOLOGY_LOOP)
		return (BFA_FCPORT_MOD(bfa))->cfg.rx_bbcredit;

	else
		return 0;
}

void
bfa_fcport_set_tx_bbcredit(struct bfa_s *bfa, u16 tx_bbcredit)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	fcport->cfg.tx_bbcredit = (u8)tx_bbcredit;
}

/*
 * Get port attributes.
 */

wwn_t
bfa_fcport_get_wwn(struct bfa_s *bfa, bfa_boolean_t node)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	if (node)
		return fcport->nwwn;
	else
		return fcport->pwwn;
}

void
bfa_fcport_get_attr(struct bfa_s *bfa, struct bfa_port_attr_s *attr)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	memset(attr, 0, sizeof(struct bfa_port_attr_s));

	attr->nwwn = fcport->nwwn;
	attr->pwwn = fcport->pwwn;

	attr->factorypwwn =  bfa->ioc.attr->mfg_pwwn;
	attr->factorynwwn =  bfa->ioc.attr->mfg_nwwn;

	memcpy(&attr->pport_cfg, &fcport->cfg,
		sizeof(struct bfa_port_cfg_s));
	/* speed attributes */
	attr->pport_cfg.speed = fcport->cfg.speed;
	attr->speed_supported = fcport->speed_sup;
	attr->speed = fcport->speed;
	attr->cos_supported = FC_CLASS_3;

	/* topology attributes */
	attr->pport_cfg.topology = fcport->cfg.topology;
	attr->topology = fcport->topology;
	attr->pport_cfg.trunked = fcport->cfg.trunked;

	/* beacon attributes */
	attr->beacon = fcport->beacon;
	attr->link_e2e_beacon = fcport->link_e2e_beacon;

	attr->pport_cfg.path_tov  = bfa_fcpim_path_tov_get(bfa);
	attr->pport_cfg.q_depth  = bfa_fcpim_qdepth_get(bfa);
	attr->port_state = bfa_sm_to_state(hal_port_sm_table, fcport->sm);

	attr->fec_state = fcport->fec_state;

	/* PBC Disabled State */
	if (bfa_fcport_is_pbcdisabled(bfa))
		attr->port_state = BFA_PORT_ST_PREBOOT_DISABLED;
	else {
		if (bfa_ioc_is_disabled(&fcport->bfa->ioc))
			attr->port_state = BFA_PORT_ST_IOCDIS;
		else if (bfa_ioc_fw_mismatch(&fcport->bfa->ioc))
			attr->port_state = BFA_PORT_ST_FWMISMATCH;
	}

	/* FCoE vlan */
	attr->fcoe_vlan = fcport->fcoe_vlan;
}

#define BFA_FCPORT_STATS_TOV	1000

/*
 * Fetch port statistics (FCQoS or FCoE).
 */
bfa_status_t
bfa_fcport_get_stats(struct bfa_s *bfa, struct bfa_cb_pending_q_s *cb)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	if (!bfa_iocfc_is_operational(bfa) ||
	    !fcport->stats_dma_ready)
		return BFA_STATUS_IOC_NON_OP;

	if (!list_empty(&fcport->statsclr_pending_q))
		return BFA_STATUS_DEVBUSY;

	if (list_empty(&fcport->stats_pending_q)) {
		list_add_tail(&cb->hcb_qe.qe, &fcport->stats_pending_q);
		bfa_fcport_send_stats_get(fcport);
		bfa_timer_start(bfa, &fcport->timer,
				bfa_fcport_stats_get_timeout,
				fcport, BFA_FCPORT_STATS_TOV);
	} else
		list_add_tail(&cb->hcb_qe.qe, &fcport->stats_pending_q);

	return BFA_STATUS_OK;
}

/*
 * Reset port statistics (FCQoS or FCoE).
 */
bfa_status_t
bfa_fcport_clear_stats(struct bfa_s *bfa, struct bfa_cb_pending_q_s *cb)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	if (!bfa_iocfc_is_operational(bfa) ||
	    !fcport->stats_dma_ready)
		return BFA_STATUS_IOC_NON_OP;

	if (!list_empty(&fcport->stats_pending_q))
		return BFA_STATUS_DEVBUSY;

	if (list_empty(&fcport->statsclr_pending_q)) {
		list_add_tail(&cb->hcb_qe.qe, &fcport->statsclr_pending_q);
		bfa_fcport_send_stats_clear(fcport);
		bfa_timer_start(bfa, &fcport->timer,
				bfa_fcport_stats_clr_timeout,
				fcport, BFA_FCPORT_STATS_TOV);
	} else
		list_add_tail(&cb->hcb_qe.qe, &fcport->statsclr_pending_q);

	return BFA_STATUS_OK;
}

/*
 * Fetch port attributes.
 */
bfa_boolean_t
bfa_fcport_is_disabled(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return bfa_sm_to_state(hal_port_sm_table, fcport->sm) ==
		BFA_PORT_ST_DISABLED;

}

bfa_boolean_t
bfa_fcport_is_dport(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return (bfa_sm_to_state(hal_port_sm_table, fcport->sm) ==
		BFA_PORT_ST_DPORT);
}

bfa_boolean_t
bfa_fcport_is_ddport(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return (bfa_sm_to_state(hal_port_sm_table, fcport->sm) ==
		BFA_PORT_ST_DDPORT);
}

bfa_status_t
bfa_fcport_set_qos_bw(struct bfa_s *bfa, struct bfa_qos_bw_s *qos_bw)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	enum bfa_ioc_type_e ioc_type = bfa_get_type(bfa);

	bfa_trc(bfa, ioc_type);

	if ((qos_bw->high == 0) || (qos_bw->med == 0) || (qos_bw->low == 0))
		return BFA_STATUS_QOS_BW_INVALID;

	if ((qos_bw->high + qos_bw->med + qos_bw->low) != 100)
		return BFA_STATUS_QOS_BW_INVALID;

	if ((qos_bw->med > qos_bw->high) || (qos_bw->low > qos_bw->med) ||
	    (qos_bw->low > qos_bw->high))
		return BFA_STATUS_QOS_BW_INVALID;

	if ((ioc_type == BFA_IOC_TYPE_FC) &&
	    (fcport->cfg.topology != BFA_PORT_TOPOLOGY_LOOP))
		fcport->cfg.qos_bw = *qos_bw;

	return BFA_STATUS_OK;
}

bfa_boolean_t
bfa_fcport_is_ratelim(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->cfg.ratelimit ? BFA_TRUE : BFA_FALSE;

}

/*
 *	Enable/Disable FAA feature in port config
 */
void
bfa_fcport_cfg_faa(struct bfa_s *bfa, u8 state)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, state);
	fcport->cfg.faa_state = state;
}

/*
 * Get default minimum ratelim speed
 */
enum bfa_port_speed
bfa_fcport_get_ratelim_speed(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, fcport->cfg.trl_def_speed);
	return fcport->cfg.trl_def_speed;

}

void
bfa_fcport_beacon(void *dev, bfa_boolean_t beacon,
		  bfa_boolean_t link_e2e_beacon)
{
	struct bfa_s *bfa = dev;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, beacon);
	bfa_trc(bfa, link_e2e_beacon);
	bfa_trc(bfa, fcport->beacon);
	bfa_trc(bfa, fcport->link_e2e_beacon);

	fcport->beacon = beacon;
	fcport->link_e2e_beacon = link_e2e_beacon;
}

bfa_boolean_t
bfa_fcport_is_linkup(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return	(!fcport->cfg.trunked &&
		 bfa_sm_cmp_state(fcport, bfa_fcport_sm_linkup)) ||
		(fcport->cfg.trunked &&
		 fcport->trunk.attr.state == BFA_TRUNK_ONLINE);
}

bfa_boolean_t
bfa_fcport_is_qos_enabled(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->cfg.qos_enabled;
}

bfa_boolean_t
bfa_fcport_is_trunk_enabled(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->cfg.trunked;
}

bfa_status_t
bfa_fcport_cfg_bbcr(struct bfa_s *bfa, bfa_boolean_t on_off, u8 bb_scn)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, on_off);

	if (bfa_ioc_get_type(&fcport->bfa->ioc) != BFA_IOC_TYPE_FC)
		return BFA_STATUS_BBCR_FC_ONLY;

	if (bfa_mfg_is_mezz(bfa->ioc.attr->card_type) &&
		(bfa->ioc.attr->card_type != BFA_MFG_TYPE_CHINOOK))
		return BFA_STATUS_CMD_NOTSUPP_MEZZ;

	if (on_off) {
		if (fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP)
			return BFA_STATUS_TOPOLOGY_LOOP;

		if (fcport->cfg.qos_enabled)
			return BFA_STATUS_ERROR_QOS_ENABLED;

		if (fcport->cfg.trunked)
			return BFA_STATUS_TRUNK_ENABLED;

		if ((fcport->cfg.speed != BFA_PORT_SPEED_AUTO) &&
			(fcport->cfg.speed < bfa_ioc_speed_sup(&bfa->ioc)))
			return BFA_STATUS_ERR_BBCR_SPEED_UNSUPPORT;

		if (bfa_ioc_speed_sup(&bfa->ioc) < BFA_PORT_SPEED_8GBPS)
			return BFA_STATUS_FEATURE_NOT_SUPPORTED;

		if (fcport->cfg.bb_cr_enabled) {
			if (bb_scn != fcport->cfg.bb_scn)
				return BFA_STATUS_BBCR_CFG_NO_CHANGE;
			else
				return BFA_STATUS_NO_CHANGE;
		}

		if ((bb_scn == 0) || (bb_scn > BFA_BB_SCN_MAX))
			bb_scn = BFA_BB_SCN_DEF;

		fcport->cfg.bb_cr_enabled = on_off;
		fcport->cfg.bb_scn = bb_scn;
	} else {
		if (!fcport->cfg.bb_cr_enabled)
			return BFA_STATUS_NO_CHANGE;

		fcport->cfg.bb_cr_enabled = on_off;
		fcport->cfg.bb_scn = 0;
	}

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcport_get_bbcr_attr(struct bfa_s *bfa,
		struct bfa_bbcr_attr_s *bbcr_attr)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	if (bfa_ioc_get_type(&fcport->bfa->ioc) != BFA_IOC_TYPE_FC)
		return BFA_STATUS_BBCR_FC_ONLY;

	if (fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP)
		return BFA_STATUS_TOPOLOGY_LOOP;

	*bbcr_attr = fcport->bbcr_attr;

	return BFA_STATUS_OK;
}

void
bfa_fcport_dportenable(struct bfa_s *bfa)
{
	/*
	 * Assume caller check for port is in disable state
	 */
	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_DPORTENABLE);
	bfa_port_set_dportenabled(&bfa->modules.port, BFA_TRUE);
}

void
bfa_fcport_dportdisable(struct bfa_s *bfa)
{
	/*
	 * Assume caller check for port is in disable state
	 */
	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_DPORTDISABLE);
	bfa_port_set_dportenabled(&bfa->modules.port, BFA_FALSE);
}

void
bfa_fcport_ddportenable(struct bfa_s *bfa)
{
	/*
	 * Assume caller check for port is in disable state
	 */
	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_DDPORTENABLE);
}

void
bfa_fcport_ddportdisable(struct bfa_s *bfa)
{
	/*
	 * Assume caller check for port is in disable state
	 */
	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_DDPORTDISABLE);
}

/*
 * Rport State machine functions
 */
/*
 * Beginning state, only online event expected.
 */
static void
bfa_rport_sm_uninit(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_CREATE:
		bfa_stats(rp, sm_un_cr);
		bfa_sm_set_state(rp, bfa_rport_sm_created);
		break;

	default:
		bfa_stats(rp, sm_un_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

static void
bfa_rport_sm_created(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_ONLINE:
		bfa_stats(rp, sm_cr_on);
		if (bfa_rport_send_fwcreate(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate_qfull);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_cr_del);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_cr_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		break;

	default:
		bfa_stats(rp, sm_cr_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/*
 * Waiting for rport create response from firmware.
 */
static void
bfa_rport_sm_fwcreate(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_fwc_rsp);
		bfa_sm_set_state(rp, bfa_rport_sm_online);
		bfa_rport_online_cb(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_fwc_del);
		bfa_sm_set_state(rp, bfa_rport_sm_delete_pending);
		break;

	case BFA_RPORT_SM_OFFLINE:
		bfa_stats(rp, sm_fwc_off);
		bfa_sm_set_state(rp, bfa_rport_sm_offline_pending);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_fwc_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		break;

	default:
		bfa_stats(rp, sm_fwc_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/*
 * Request queue is full, awaiting queue resume to send create request.
 */
static void
bfa_rport_sm_fwcreate_qfull(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_QRESUME:
		bfa_sm_set_state(rp, bfa_rport_sm_fwcreate);
		bfa_rport_send_fwcreate(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_fwc_del);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_reqq_wcancel(&rp->reqq_wait);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_OFFLINE:
		bfa_stats(rp, sm_fwc_off);
		bfa_sm_set_state(rp, bfa_rport_sm_offline);
		bfa_reqq_wcancel(&rp->reqq_wait);
		bfa_rport_offline_cb(rp);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_fwc_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		bfa_reqq_wcancel(&rp->reqq_wait);
		break;

	default:
		bfa_stats(rp, sm_fwc_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/*
 * Online state - normal parking state.
 */
static void
bfa_rport_sm_online(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	struct bfi_rport_qos_scn_s *qos_scn;

	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_OFFLINE:
		bfa_stats(rp, sm_on_off);
		if (bfa_rport_send_fwdelete(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwdelete);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwdelete_qfull);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_on_del);
		if (bfa_rport_send_fwdelete(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_deleting);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_deleting_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_on_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		break;

	case BFA_RPORT_SM_SET_SPEED:
		bfa_rport_send_fwspeed(rp);
		break;

	case BFA_RPORT_SM_QOS_SCN:
		qos_scn = (struct bfi_rport_qos_scn_s *) rp->event_arg.fw_msg;
		rp->qos_attr = qos_scn->new_qos_attr;
		bfa_trc(rp->bfa, qos_scn->old_qos_attr.qos_flow_id);
		bfa_trc(rp->bfa, qos_scn->new_qos_attr.qos_flow_id);
		bfa_trc(rp->bfa, qos_scn->old_qos_attr.qos_priority);
		bfa_trc(rp->bfa, qos_scn->new_qos_attr.qos_priority);

		qos_scn->old_qos_attr.qos_flow_id  =
			be32_to_cpu(qos_scn->old_qos_attr.qos_flow_id);
		qos_scn->new_qos_attr.qos_flow_id  =
			be32_to_cpu(qos_scn->new_qos_attr.qos_flow_id);

		if (qos_scn->old_qos_attr.qos_flow_id !=
			qos_scn->new_qos_attr.qos_flow_id)
			bfa_cb_rport_qos_scn_flowid(rp->rport_drv,
						    qos_scn->old_qos_attr,
						    qos_scn->new_qos_attr);
		if (qos_scn->old_qos_attr.qos_priority !=
			qos_scn->new_qos_attr.qos_priority)
			bfa_cb_rport_qos_scn_prio(rp->rport_drv,
						  qos_scn->old_qos_attr,
						  qos_scn->new_qos_attr);
		break;

	default:
		bfa_stats(rp, sm_on_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/*
 * Firmware rport is being deleted - awaiting f/w response.
 */
static void
bfa_rport_sm_fwdelete(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_fwd_rsp);
		bfa_sm_set_state(rp, bfa_rport_sm_offline);
		bfa_rport_offline_cb(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_fwd_del);
		bfa_sm_set_state(rp, bfa_rport_sm_deleting);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_fwd_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		bfa_rport_offline_cb(rp);
		break;

	default:
		bfa_stats(rp, sm_fwd_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

static void
bfa_rport_sm_fwdelete_qfull(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_QRESUME:
		bfa_sm_set_state(rp, bfa_rport_sm_fwdelete);
		bfa_rport_send_fwdelete(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_fwd_del);
		bfa_sm_set_state(rp, bfa_rport_sm_deleting_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_fwd_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		bfa_reqq_wcancel(&rp->reqq_wait);
		bfa_rport_offline_cb(rp);
		break;

	default:
		bfa_stats(rp, sm_fwd_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/*
 * Offline state.
 */
static void
bfa_rport_sm_offline(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_off_del);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_ONLINE:
		bfa_stats(rp, sm_off_on);
		if (bfa_rport_send_fwcreate(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_off_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		break;

	case BFA_RPORT_SM_OFFLINE:
		bfa_rport_offline_cb(rp);
		break;

	default:
		bfa_stats(rp, sm_off_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/*
 * Rport is deleted, waiting for firmware response to delete.
 */
static void
bfa_rport_sm_deleting(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_del_fwrsp);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_del_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	default:
		bfa_sm_fault(rp->bfa, event);
	}
}

static void
bfa_rport_sm_deleting_qfull(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_QRESUME:
		bfa_stats(rp, sm_del_fwrsp);
		bfa_sm_set_state(rp, bfa_rport_sm_deleting);
		bfa_rport_send_fwdelete(rp);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_del_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_reqq_wcancel(&rp->reqq_wait);
		bfa_rport_free(rp);
		break;

	default:
		bfa_sm_fault(rp->bfa, event);
	}
}

/*
 * Waiting for rport create response from firmware. A delete is pending.
 */
static void
bfa_rport_sm_delete_pending(struct bfa_rport_s *rp,
				enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_delp_fwrsp);
		if (bfa_rport_send_fwdelete(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_deleting);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_deleting_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_delp_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	default:
		bfa_stats(rp, sm_delp_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/*
 * Waiting for rport create response from firmware. Rport offline is pending.
 */
static void
bfa_rport_sm_offline_pending(struct bfa_rport_s *rp,
				 enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_offp_fwrsp);
		if (bfa_rport_send_fwdelete(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwdelete);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwdelete_qfull);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_offp_del);
		bfa_sm_set_state(rp, bfa_rport_sm_delete_pending);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_offp_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		bfa_rport_offline_cb(rp);
		break;

	default:
		bfa_stats(rp, sm_offp_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/*
 * IOC h/w failed.
 */
static void
bfa_rport_sm_iocdisable(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_OFFLINE:
		bfa_stats(rp, sm_iocd_off);
		bfa_rport_offline_cb(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_iocd_del);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_ONLINE:
		bfa_stats(rp, sm_iocd_on);
		if (bfa_rport_send_fwcreate(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		break;

	default:
		bfa_stats(rp, sm_iocd_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}



/*
 *  bfa_rport_private BFA rport private functions
 */

static void
__bfa_cb_rport_online(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_rport_s *rp = cbarg;

	if (complete)
		bfa_cb_rport_online(rp->rport_drv);
}

static void
__bfa_cb_rport_offline(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_rport_s *rp = cbarg;

	if (complete)
		bfa_cb_rport_offline(rp->rport_drv);
}

static void
bfa_rport_qresume(void *cbarg)
{
	struct bfa_rport_s	*rp = cbarg;

	bfa_sm_send_event(rp, BFA_RPORT_SM_QRESUME);
}

void
bfa_rport_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *minfo,
		struct bfa_s *bfa)
{
	struct bfa_mem_kva_s *rport_kva = BFA_MEM_RPORT_KVA(bfa);

	if (cfg->fwcfg.num_rports < BFA_RPORT_MIN)
		cfg->fwcfg.num_rports = BFA_RPORT_MIN;

	/* kva memory */
	bfa_mem_kva_setup(minfo, rport_kva,
		cfg->fwcfg.num_rports * sizeof(struct bfa_rport_s));
}

void
bfa_rport_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_pcidev_s *pcidev)
{
	struct bfa_rport_mod_s *mod = BFA_RPORT_MOD(bfa);
	struct bfa_rport_s *rp;
	u16 i;

	INIT_LIST_HEAD(&mod->rp_free_q);
	INIT_LIST_HEAD(&mod->rp_active_q);
	INIT_LIST_HEAD(&mod->rp_unused_q);

	rp = (struct bfa_rport_s *) bfa_mem_kva_curp(mod);
	mod->rps_list = rp;
	mod->num_rports = cfg->fwcfg.num_rports;

	WARN_ON(!mod->num_rports ||
		   (mod->num_rports & (mod->num_rports - 1)));

	for (i = 0; i < mod->num_rports; i++, rp++) {
		memset(rp, 0, sizeof(struct bfa_rport_s));
		rp->bfa = bfa;
		rp->rport_tag = i;
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);

		/*
		 *  - is unused
		 */
		if (i)
			list_add_tail(&rp->qe, &mod->rp_free_q);

		bfa_reqq_winit(&rp->reqq_wait, bfa_rport_qresume, rp);
	}

	/*
	 * consume memory
	 */
	bfa_mem_kva_curp(mod) = (u8 *) rp;
}

void
bfa_rport_iocdisable(struct bfa_s *bfa)
{
	struct bfa_rport_mod_s *mod = BFA_RPORT_MOD(bfa);
	struct bfa_rport_s *rport;
	struct list_head *qe, *qen;

	/* Enqueue unused rport resources to free_q */
	list_splice_tail_init(&mod->rp_unused_q, &mod->rp_free_q);

	list_for_each_safe(qe, qen, &mod->rp_active_q) {
		rport = (struct bfa_rport_s *) qe;
		bfa_sm_send_event(rport, BFA_RPORT_SM_HWFAIL);
	}
}

static struct bfa_rport_s *
bfa_rport_alloc(struct bfa_rport_mod_s *mod)
{
	struct bfa_rport_s *rport;

	bfa_q_deq(&mod->rp_free_q, &rport);
	if (rport)
		list_add_tail(&rport->qe, &mod->rp_active_q);

	return rport;
}

static void
bfa_rport_free(struct bfa_rport_s *rport)
{
	struct bfa_rport_mod_s *mod = BFA_RPORT_MOD(rport->bfa);

	WARN_ON(!bfa_q_is_on_q(&mod->rp_active_q, rport));
	list_del(&rport->qe);
	list_add_tail(&rport->qe, &mod->rp_free_q);
}

static bfa_boolean_t
bfa_rport_send_fwcreate(struct bfa_rport_s *rp)
{
	struct bfi_rport_create_req_s *m;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(rp->bfa, BFA_REQQ_RPORT);
	if (!m) {
		bfa_reqq_wait(rp->bfa, BFA_REQQ_RPORT, &rp->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_RPORT, BFI_RPORT_H2I_CREATE_REQ,
			bfa_fn_lpu(rp->bfa));
	m->bfa_handle = rp->rport_tag;
	m->max_frmsz = cpu_to_be16(rp->rport_info.max_frmsz);
	m->pid = rp->rport_info.pid;
	m->lp_fwtag = bfa_lps_get_fwtag(rp->bfa, (u8)rp->rport_info.lp_tag);
	m->local_pid = rp->rport_info.local_pid;
	m->fc_class = rp->rport_info.fc_class;
	m->vf_en = rp->rport_info.vf_en;
	m->vf_id = rp->rport_info.vf_id;
	m->cisc = rp->rport_info.cisc;

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(rp->bfa, BFA_REQQ_RPORT, m->mh);
	return BFA_TRUE;
}

static bfa_boolean_t
bfa_rport_send_fwdelete(struct bfa_rport_s *rp)
{
	struct bfi_rport_delete_req_s *m;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(rp->bfa, BFA_REQQ_RPORT);
	if (!m) {
		bfa_reqq_wait(rp->bfa, BFA_REQQ_RPORT, &rp->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_RPORT, BFI_RPORT_H2I_DELETE_REQ,
			bfa_fn_lpu(rp->bfa));
	m->fw_handle = rp->fw_handle;

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(rp->bfa, BFA_REQQ_RPORT, m->mh);
	return BFA_TRUE;
}

static bfa_boolean_t
bfa_rport_send_fwspeed(struct bfa_rport_s *rp)
{
	struct bfa_rport_speed_req_s *m;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(rp->bfa, BFA_REQQ_RPORT);
	if (!m) {
		bfa_trc(rp->bfa, rp->rport_info.speed);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_RPORT, BFI_RPORT_H2I_SET_SPEED_REQ,
			bfa_fn_lpu(rp->bfa));
	m->fw_handle = rp->fw_handle;
	m->speed = (u8)rp->rport_info.speed;

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(rp->bfa, BFA_REQQ_RPORT, m->mh);
	return BFA_TRUE;
}



/*
 *  bfa_rport_public
 */

/*
 * Rport interrupt processing.
 */
void
bfa_rport_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	union bfi_rport_i2h_msg_u msg;
	struct bfa_rport_s *rp;

	bfa_trc(bfa, m->mhdr.msg_id);

	msg.msg = m;

	switch (m->mhdr.msg_id) {
	case BFI_RPORT_I2H_CREATE_RSP:
		rp = BFA_RPORT_FROM_TAG(bfa, msg.create_rsp->bfa_handle);
		rp->fw_handle = msg.create_rsp->fw_handle;
		rp->qos_attr = msg.create_rsp->qos_attr;
		bfa_rport_set_lunmask(bfa, rp);
		WARN_ON(msg.create_rsp->status != BFA_STATUS_OK);
		bfa_sm_send_event(rp, BFA_RPORT_SM_FWRSP);
		break;

	case BFI_RPORT_I2H_DELETE_RSP:
		rp = BFA_RPORT_FROM_TAG(bfa, msg.delete_rsp->bfa_handle);
		WARN_ON(msg.delete_rsp->status != BFA_STATUS_OK);
		bfa_rport_unset_lunmask(bfa, rp);
		bfa_sm_send_event(rp, BFA_RPORT_SM_FWRSP);
		break;

	case BFI_RPORT_I2H_QOS_SCN:
		rp = BFA_RPORT_FROM_TAG(bfa, msg.qos_scn_evt->bfa_handle);
		rp->event_arg.fw_msg = msg.qos_scn_evt;
		bfa_sm_send_event(rp, BFA_RPORT_SM_QOS_SCN);
		break;

	case BFI_RPORT_I2H_LIP_SCN_ONLINE:
		bfa_fcport_update_loop_info(BFA_FCPORT_MOD(bfa),
				&msg.lip_scn->loop_info);
		bfa_cb_rport_scn_online(bfa);
		break;

	case BFI_RPORT_I2H_LIP_SCN_OFFLINE:
		bfa_cb_rport_scn_offline(bfa);
		break;

	case BFI_RPORT_I2H_NO_DEV:
		rp = BFA_RPORT_FROM_TAG(bfa, msg.lip_scn->bfa_handle);
		bfa_cb_rport_scn_no_dev(rp->rport_drv);
		break;

	default:
		bfa_trc(bfa, m->mhdr.msg_id);
		WARN_ON(1);
	}
}

void
bfa_rport_res_recfg(struct bfa_s *bfa, u16 num_rport_fw)
{
	struct bfa_rport_mod_s	*mod = BFA_RPORT_MOD(bfa);
	struct list_head	*qe;
	int	i;

	for (i = 0; i < (mod->num_rports - num_rport_fw); i++) {
		bfa_q_deq_tail(&mod->rp_free_q, &qe);
		list_add_tail(qe, &mod->rp_unused_q);
	}
}

/*
 *  bfa_rport_api
 */

struct bfa_rport_s *
bfa_rport_create(struct bfa_s *bfa, void *rport_drv)
{
	struct bfa_rport_s *rp;

	rp = bfa_rport_alloc(BFA_RPORT_MOD(bfa));

	if (rp == NULL)
		return NULL;

	rp->bfa = bfa;
	rp->rport_drv = rport_drv;
	memset(&rp->stats, 0, sizeof(rp->stats));

	WARN_ON(!bfa_sm_cmp_state(rp, bfa_rport_sm_uninit));
	bfa_sm_send_event(rp, BFA_RPORT_SM_CREATE);

	return rp;
}

void
bfa_rport_online(struct bfa_rport_s *rport, struct bfa_rport_info_s *rport_info)
{
	WARN_ON(rport_info->max_frmsz == 0);

	/*
	 * Some JBODs are seen to be not setting PDU size correctly in PLOGI
	 * responses. Default to minimum size.
	 */
	if (rport_info->max_frmsz == 0) {
		bfa_trc(rport->bfa, rport->rport_tag);
		rport_info->max_frmsz = FC_MIN_PDUSZ;
	}

	rport->rport_info = *rport_info;
	bfa_sm_send_event(rport, BFA_RPORT_SM_ONLINE);
}

void
bfa_rport_speed(struct bfa_rport_s *rport, enum bfa_port_speed speed)
{
	WARN_ON(speed == 0);
	WARN_ON(speed == BFA_PORT_SPEED_AUTO);

	if (rport) {
		rport->rport_info.speed = speed;
		bfa_sm_send_event(rport, BFA_RPORT_SM_SET_SPEED);
	}
}

/* Set Rport LUN Mask */
void
bfa_rport_set_lunmask(struct bfa_s *bfa, struct bfa_rport_s *rp)
{
	struct bfa_lps_mod_s	*lps_mod = BFA_LPS_MOD(bfa);
	wwn_t	lp_wwn, rp_wwn;
	u8 lp_tag = (u8)rp->rport_info.lp_tag;

	rp_wwn = ((struct bfa_fcs_rport_s *)rp->rport_drv)->pwwn;
	lp_wwn = (BFA_LPS_FROM_TAG(lps_mod, rp->rport_info.lp_tag))->pwwn;

	BFA_LPS_FROM_TAG(lps_mod, rp->rport_info.lp_tag)->lun_mask =
					rp->lun_mask = BFA_TRUE;
	bfa_fcpim_lunmask_rp_update(bfa, lp_wwn, rp_wwn, rp->rport_tag, lp_tag);
}

/* Unset Rport LUN mask */
void
bfa_rport_unset_lunmask(struct bfa_s *bfa, struct bfa_rport_s *rp)
{
	struct bfa_lps_mod_s	*lps_mod = BFA_LPS_MOD(bfa);
	wwn_t	lp_wwn, rp_wwn;

	rp_wwn = ((struct bfa_fcs_rport_s *)rp->rport_drv)->pwwn;
	lp_wwn = (BFA_LPS_FROM_TAG(lps_mod, rp->rport_info.lp_tag))->pwwn;

	BFA_LPS_FROM_TAG(lps_mod, rp->rport_info.lp_tag)->lun_mask =
				rp->lun_mask = BFA_FALSE;
	bfa_fcpim_lunmask_rp_update(bfa, lp_wwn, rp_wwn,
			BFA_RPORT_TAG_INVALID, BFA_LP_TAG_INVALID);
}

/*
 * SGPG related functions
 */

/*
 * Compute and return memory needed by FCP(im) module.
 */
void
bfa_sgpg_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *minfo,
		struct bfa_s *bfa)
{
	struct bfa_sgpg_mod_s *sgpg_mod = BFA_SGPG_MOD(bfa);
	struct bfa_mem_kva_s *sgpg_kva = BFA_MEM_SGPG_KVA(bfa);
	struct bfa_mem_dma_s *seg_ptr;
	u16	nsegs, idx, per_seg_sgpg, num_sgpg;
	u32	sgpg_sz = sizeof(struct bfi_sgpg_s);

	if (cfg->drvcfg.num_sgpgs < BFA_SGPG_MIN)
		cfg->drvcfg.num_sgpgs = BFA_SGPG_MIN;
	else if (cfg->drvcfg.num_sgpgs > BFA_SGPG_MAX)
		cfg->drvcfg.num_sgpgs = BFA_SGPG_MAX;

	num_sgpg = cfg->drvcfg.num_sgpgs;

	nsegs = BFI_MEM_DMA_NSEGS(num_sgpg, sgpg_sz);
	per_seg_sgpg = BFI_MEM_NREQS_SEG(sgpg_sz);

	bfa_mem_dma_seg_iter(sgpg_mod, seg_ptr, nsegs, idx) {
		if (num_sgpg >= per_seg_sgpg) {
			num_sgpg -= per_seg_sgpg;
			bfa_mem_dma_setup(minfo, seg_ptr,
					per_seg_sgpg * sgpg_sz);
		} else
			bfa_mem_dma_setup(minfo, seg_ptr,
					num_sgpg * sgpg_sz);
	}

	/* kva memory */
	bfa_mem_kva_setup(minfo, sgpg_kva,
		cfg->drvcfg.num_sgpgs * sizeof(struct bfa_sgpg_s));
}

void
bfa_sgpg_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_pcidev_s *pcidev)
{
	struct bfa_sgpg_mod_s *mod = BFA_SGPG_MOD(bfa);
	struct bfa_sgpg_s *hsgpg;
	struct bfi_sgpg_s *sgpg;
	u64 align_len;
	struct bfa_mem_dma_s *seg_ptr;
	u32	sgpg_sz = sizeof(struct bfi_sgpg_s);
	u16	i, idx, nsegs, per_seg_sgpg, num_sgpg;

	union {
		u64 pa;
		union bfi_addr_u addr;
	} sgpg_pa, sgpg_pa_tmp;

	INIT_LIST_HEAD(&mod->sgpg_q);
	INIT_LIST_HEAD(&mod->sgpg_wait_q);

	bfa_trc(bfa, cfg->drvcfg.num_sgpgs);

	mod->free_sgpgs = mod->num_sgpgs = cfg->drvcfg.num_sgpgs;

	num_sgpg = cfg->drvcfg.num_sgpgs;
	nsegs = BFI_MEM_DMA_NSEGS(num_sgpg, sgpg_sz);

	/* dma/kva mem claim */
	hsgpg = (struct bfa_sgpg_s *) bfa_mem_kva_curp(mod);

	bfa_mem_dma_seg_iter(mod, seg_ptr, nsegs, idx) {

		if (!bfa_mem_dma_virt(seg_ptr))
			break;

		align_len = BFA_SGPG_ROUNDUP(bfa_mem_dma_phys(seg_ptr)) -
					     bfa_mem_dma_phys(seg_ptr);

		sgpg = (struct bfi_sgpg_s *)
			(((u8 *) bfa_mem_dma_virt(seg_ptr)) + align_len);
		sgpg_pa.pa = bfa_mem_dma_phys(seg_ptr) + align_len;
		WARN_ON(sgpg_pa.pa & (sgpg_sz - 1));

		per_seg_sgpg = (seg_ptr->mem_len - (u32)align_len) / sgpg_sz;

		for (i = 0; num_sgpg > 0 && i < per_seg_sgpg; i++, num_sgpg--) {
			memset(hsgpg, 0, sizeof(*hsgpg));
			memset(sgpg, 0, sizeof(*sgpg));

			hsgpg->sgpg = sgpg;
			sgpg_pa_tmp.pa = bfa_sgaddr_le(sgpg_pa.pa);
			hsgpg->sgpg_pa = sgpg_pa_tmp.addr;
			list_add_tail(&hsgpg->qe, &mod->sgpg_q);

			sgpg++;
			hsgpg++;
			sgpg_pa.pa += sgpg_sz;
		}
	}

	bfa_mem_kva_curp(mod) = (u8 *) hsgpg;
}

bfa_status_t
bfa_sgpg_malloc(struct bfa_s *bfa, struct list_head *sgpg_q, int nsgpgs)
{
	struct bfa_sgpg_mod_s *mod = BFA_SGPG_MOD(bfa);
	struct bfa_sgpg_s *hsgpg;
	int i;

	if (mod->free_sgpgs < nsgpgs)
		return BFA_STATUS_ENOMEM;

	for (i = 0; i < nsgpgs; i++) {
		bfa_q_deq(&mod->sgpg_q, &hsgpg);
		WARN_ON(!hsgpg);
		list_add_tail(&hsgpg->qe, sgpg_q);
	}

	mod->free_sgpgs -= nsgpgs;
	return BFA_STATUS_OK;
}

void
bfa_sgpg_mfree(struct bfa_s *bfa, struct list_head *sgpg_q, int nsgpg)
{
	struct bfa_sgpg_mod_s *mod = BFA_SGPG_MOD(bfa);
	struct bfa_sgpg_wqe_s *wqe;

	mod->free_sgpgs += nsgpg;
	WARN_ON(mod->free_sgpgs > mod->num_sgpgs);

	list_splice_tail_init(sgpg_q, &mod->sgpg_q);

	if (list_empty(&mod->sgpg_wait_q))
		return;

	/*
	 * satisfy as many waiting requests as possible
	 */
	do {
		wqe = bfa_q_first(&mod->sgpg_wait_q);
		if (mod->free_sgpgs < wqe->nsgpg)
			nsgpg = mod->free_sgpgs;
		else
			nsgpg = wqe->nsgpg;
		bfa_sgpg_malloc(bfa, &wqe->sgpg_q, nsgpg);
		wqe->nsgpg -= nsgpg;
		if (wqe->nsgpg == 0) {
			list_del(&wqe->qe);
			wqe->cbfn(wqe->cbarg);
		}
	} while (mod->free_sgpgs && !list_empty(&mod->sgpg_wait_q));
}

void
bfa_sgpg_wait(struct bfa_s *bfa, struct bfa_sgpg_wqe_s *wqe, int nsgpg)
{
	struct bfa_sgpg_mod_s *mod = BFA_SGPG_MOD(bfa);

	WARN_ON(nsgpg <= 0);
	WARN_ON(nsgpg <= mod->free_sgpgs);

	wqe->nsgpg_total = wqe->nsgpg = nsgpg;

	/*
	 * allocate any left to this one first
	 */
	if (mod->free_sgpgs) {
		/*
		 * no one else is waiting for SGPG
		 */
		WARN_ON(!list_empty(&mod->sgpg_wait_q));
		list_splice_tail_init(&mod->sgpg_q, &wqe->sgpg_q);
		wqe->nsgpg -= mod->free_sgpgs;
		mod->free_sgpgs = 0;
	}

	list_add_tail(&wqe->qe, &mod->sgpg_wait_q);
}

void
bfa_sgpg_wcancel(struct bfa_s *bfa, struct bfa_sgpg_wqe_s *wqe)
{
	struct bfa_sgpg_mod_s *mod = BFA_SGPG_MOD(bfa);

	WARN_ON(!bfa_q_is_on_q(&mod->sgpg_wait_q, wqe));
	list_del(&wqe->qe);

	if (wqe->nsgpg_total != wqe->nsgpg)
		bfa_sgpg_mfree(bfa, &wqe->sgpg_q,
				   wqe->nsgpg_total - wqe->nsgpg);
}

void
bfa_sgpg_winit(struct bfa_sgpg_wqe_s *wqe, void (*cbfn) (void *cbarg),
		   void *cbarg)
{
	INIT_LIST_HEAD(&wqe->sgpg_q);
	wqe->cbfn = cbfn;
	wqe->cbarg = cbarg;
}

/*
 *  UF related functions
 */
/*
 *****************************************************************************
 * Internal functions
 *****************************************************************************
 */
static void
__bfa_cb_uf_recv(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_uf_s   *uf = cbarg;
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(uf->bfa);

	if (complete)
		ufm->ufrecv(ufm->cbarg, uf);
}

static void
claim_uf_post_msgs(struct bfa_uf_mod_s *ufm)
{
	struct bfi_uf_buf_post_s *uf_bp_msg;
	u16 i;
	u16 buf_len;

	ufm->uf_buf_posts = (struct bfi_uf_buf_post_s *) bfa_mem_kva_curp(ufm);
	uf_bp_msg = ufm->uf_buf_posts;

	for (i = 0, uf_bp_msg = ufm->uf_buf_posts; i < ufm->num_ufs;
	     i++, uf_bp_msg++) {
		memset(uf_bp_msg, 0, sizeof(struct bfi_uf_buf_post_s));

		uf_bp_msg->buf_tag = i;
		buf_len = sizeof(struct bfa_uf_buf_s);
		uf_bp_msg->buf_len = cpu_to_be16(buf_len);
		bfi_h2i_set(uf_bp_msg->mh, BFI_MC_UF, BFI_UF_H2I_BUF_POST,
			    bfa_fn_lpu(ufm->bfa));
		bfa_alen_set(&uf_bp_msg->alen, buf_len, ufm_pbs_pa(ufm, i));
	}

	/*
	 * advance pointer beyond consumed memory
	 */
	bfa_mem_kva_curp(ufm) = (u8 *) uf_bp_msg;
}

static void
claim_ufs(struct bfa_uf_mod_s *ufm)
{
	u16 i;
	struct bfa_uf_s   *uf;

	/*
	 * Claim block of memory for UF list
	 */
	ufm->uf_list = (struct bfa_uf_s *) bfa_mem_kva_curp(ufm);

	/*
	 * Initialize UFs and queue it in UF free queue
	 */
	for (i = 0, uf = ufm->uf_list; i < ufm->num_ufs; i++, uf++) {
		memset(uf, 0, sizeof(struct bfa_uf_s));
		uf->bfa = ufm->bfa;
		uf->uf_tag = i;
		uf->pb_len = BFA_PER_UF_DMA_SZ;
		uf->buf_kva = bfa_mem_get_dmabuf_kva(ufm, i, BFA_PER_UF_DMA_SZ);
		uf->buf_pa = ufm_pbs_pa(ufm, i);
		list_add_tail(&uf->qe, &ufm->uf_free_q);
	}

	/*
	 * advance memory pointer
	 */
	bfa_mem_kva_curp(ufm) = (u8 *) uf;
}

static void
uf_mem_claim(struct bfa_uf_mod_s *ufm)
{
	claim_ufs(ufm);
	claim_uf_post_msgs(ufm);
}

void
bfa_uf_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *minfo,
		struct bfa_s *bfa)
{
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(bfa);
	struct bfa_mem_kva_s *uf_kva = BFA_MEM_UF_KVA(bfa);
	u32	num_ufs = cfg->fwcfg.num_uf_bufs;
	struct bfa_mem_dma_s *seg_ptr;
	u16	nsegs, idx, per_seg_uf = 0;

	nsegs = BFI_MEM_DMA_NSEGS(num_ufs, BFA_PER_UF_DMA_SZ);
	per_seg_uf = BFI_MEM_NREQS_SEG(BFA_PER_UF_DMA_SZ);

	bfa_mem_dma_seg_iter(ufm, seg_ptr, nsegs, idx) {
		if (num_ufs >= per_seg_uf) {
			num_ufs -= per_seg_uf;
			bfa_mem_dma_setup(minfo, seg_ptr,
				per_seg_uf * BFA_PER_UF_DMA_SZ);
		} else
			bfa_mem_dma_setup(minfo, seg_ptr,
				num_ufs * BFA_PER_UF_DMA_SZ);
	}

	/* kva memory */
	bfa_mem_kva_setup(minfo, uf_kva, cfg->fwcfg.num_uf_bufs *
		(sizeof(struct bfa_uf_s) + sizeof(struct bfi_uf_buf_post_s)));
}

void
bfa_uf_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_pcidev_s *pcidev)
{
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(bfa);

	ufm->bfa = bfa;
	ufm->num_ufs = cfg->fwcfg.num_uf_bufs;
	INIT_LIST_HEAD(&ufm->uf_free_q);
	INIT_LIST_HEAD(&ufm->uf_posted_q);
	INIT_LIST_HEAD(&ufm->uf_unused_q);

	uf_mem_claim(ufm);
}

static struct bfa_uf_s *
bfa_uf_get(struct bfa_uf_mod_s *uf_mod)
{
	struct bfa_uf_s   *uf;

	bfa_q_deq(&uf_mod->uf_free_q, &uf);
	return uf;
}

static void
bfa_uf_put(struct bfa_uf_mod_s *uf_mod, struct bfa_uf_s *uf)
{
	list_add_tail(&uf->qe, &uf_mod->uf_free_q);
}

static bfa_status_t
bfa_uf_post(struct bfa_uf_mod_s *ufm, struct bfa_uf_s *uf)
{
	struct bfi_uf_buf_post_s *uf_post_msg;

	uf_post_msg = bfa_reqq_next(ufm->bfa, BFA_REQQ_FCXP);
	if (!uf_post_msg)
		return BFA_STATUS_FAILED;

	memcpy(uf_post_msg, &ufm->uf_buf_posts[uf->uf_tag],
		      sizeof(struct bfi_uf_buf_post_s));
	bfa_reqq_produce(ufm->bfa, BFA_REQQ_FCXP, uf_post_msg->mh);

	bfa_trc(ufm->bfa, uf->uf_tag);

	list_add_tail(&uf->qe, &ufm->uf_posted_q);
	return BFA_STATUS_OK;
}

static void
bfa_uf_post_all(struct bfa_uf_mod_s *uf_mod)
{
	struct bfa_uf_s   *uf;

	while ((uf = bfa_uf_get(uf_mod)) != NULL) {
		if (bfa_uf_post(uf_mod, uf) != BFA_STATUS_OK)
			break;
	}
}

static void
uf_recv(struct bfa_s *bfa, struct bfi_uf_frm_rcvd_s *m)
{
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(bfa);
	u16 uf_tag = m->buf_tag;
	struct bfa_uf_s *uf = &ufm->uf_list[uf_tag];
	struct bfa_uf_buf_s *uf_buf;
	uint8_t *buf;
	struct fchs_s *fchs;

	uf_buf = (struct bfa_uf_buf_s *)
			bfa_mem_get_dmabuf_kva(ufm, uf_tag, uf->pb_len);
	buf = &uf_buf->d[0];

	m->frm_len = be16_to_cpu(m->frm_len);
	m->xfr_len = be16_to_cpu(m->xfr_len);

	fchs = (struct fchs_s *)uf_buf;

	list_del(&uf->qe);	/* dequeue from posted queue */

	uf->data_ptr = buf;
	uf->data_len = m->xfr_len;

	WARN_ON(uf->data_len < sizeof(struct fchs_s));

	if (uf->data_len == sizeof(struct fchs_s)) {
		bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_UF, BFA_PL_EID_RX,
			       uf->data_len, (struct fchs_s *)buf);
	} else {
		u32 pld_w0 = *((u32 *) (buf + sizeof(struct fchs_s)));
		bfa_plog_fchdr_and_pl(bfa->plog, BFA_PL_MID_HAL_UF,
				      BFA_PL_EID_RX, uf->data_len,
				      (struct fchs_s *)buf, pld_w0);
	}

	if (bfa->fcs)
		__bfa_cb_uf_recv(uf, BFA_TRUE);
	else
		bfa_cb_queue(bfa, &uf->hcb_qe, __bfa_cb_uf_recv, uf);
}

void
bfa_uf_iocdisable(struct bfa_s *bfa)
{
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(bfa);
	struct bfa_uf_s *uf;
	struct list_head *qe, *qen;

	/* Enqueue unused uf resources to free_q */
	list_splice_tail_init(&ufm->uf_unused_q, &ufm->uf_free_q);

	list_for_each_safe(qe, qen, &ufm->uf_posted_q) {
		uf = (struct bfa_uf_s *) qe;
		list_del(&uf->qe);
		bfa_uf_put(ufm, uf);
	}
}

void
bfa_uf_start(struct bfa_s *bfa)
{
	bfa_uf_post_all(BFA_UF_MOD(bfa));
}

/*
 * Register handler for all unsolicted receive frames.
 *
 * @param[in]	bfa		BFA instance
 * @param[in]	ufrecv	receive handler function
 * @param[in]	cbarg	receive handler arg
 */
void
bfa_uf_recv_register(struct bfa_s *bfa, bfa_cb_uf_recv_t ufrecv, void *cbarg)
{
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(bfa);

	ufm->ufrecv = ufrecv;
	ufm->cbarg = cbarg;
}

/*
 *	Free an unsolicited frame back to BFA.
 *
 * @param[in]		uf		unsolicited frame to be freed
 *
 * @return None
 */
void
bfa_uf_free(struct bfa_uf_s *uf)
{
	bfa_uf_put(BFA_UF_MOD(uf->bfa), uf);
	bfa_uf_post_all(BFA_UF_MOD(uf->bfa));
}



/*
 *  uf_pub BFA uf module public functions
 */
void
bfa_uf_isr(struct bfa_s *bfa, struct bfi_msg_s *msg)
{
	bfa_trc(bfa, msg->mhdr.msg_id);

	switch (msg->mhdr.msg_id) {
	case BFI_UF_I2H_FRM_RCVD:
		uf_recv(bfa, (struct bfi_uf_frm_rcvd_s *) msg);
		break;

	default:
		bfa_trc(bfa, msg->mhdr.msg_id);
		WARN_ON(1);
	}
}

void
bfa_uf_res_recfg(struct bfa_s *bfa, u16 num_uf_fw)
{
	struct bfa_uf_mod_s	*mod = BFA_UF_MOD(bfa);
	struct list_head	*qe;
	int	i;

	for (i = 0; i < (mod->num_ufs - num_uf_fw); i++) {
		bfa_q_deq_tail(&mod->uf_free_q, &qe);
		list_add_tail(qe, &mod->uf_unused_q);
	}
}

/*
 *	Dport forward declaration
 */

enum bfa_dport_test_state_e {
	BFA_DPORT_ST_DISABLED	= 0,	/*!< dport is disabled */
	BFA_DPORT_ST_INP	= 1,	/*!< test in progress */
	BFA_DPORT_ST_COMP	= 2,	/*!< test complete successfully */
	BFA_DPORT_ST_NO_SFP	= 3,	/*!< sfp is not present */
	BFA_DPORT_ST_NOTSTART	= 4,	/*!< test not start dport is enabled */
};

/*
 * BFA DPORT state machine events
 */
enum bfa_dport_sm_event {
	BFA_DPORT_SM_ENABLE	= 1,	/* dport enable event         */
	BFA_DPORT_SM_DISABLE    = 2,    /* dport disable event        */
	BFA_DPORT_SM_FWRSP      = 3,    /* fw enable/disable rsp      */
	BFA_DPORT_SM_QRESUME    = 4,    /* CQ space available         */
	BFA_DPORT_SM_HWFAIL     = 5,    /* IOC h/w failure            */
	BFA_DPORT_SM_START	= 6,	/* re-start dport test        */
	BFA_DPORT_SM_REQFAIL	= 7,	/* request failure            */
	BFA_DPORT_SM_SCN	= 8,	/* state change notify frm fw */
};

static void bfa_dport_sm_disabled(struct bfa_dport_s *dport,
				  enum bfa_dport_sm_event event);
static void bfa_dport_sm_enabling_qwait(struct bfa_dport_s *dport,
				  enum bfa_dport_sm_event event);
static void bfa_dport_sm_enabling(struct bfa_dport_s *dport,
				  enum bfa_dport_sm_event event);
static void bfa_dport_sm_enabled(struct bfa_dport_s *dport,
				 enum bfa_dport_sm_event event);
static void bfa_dport_sm_disabling_qwait(struct bfa_dport_s *dport,
				 enum bfa_dport_sm_event event);
static void bfa_dport_sm_disabling(struct bfa_dport_s *dport,
				   enum bfa_dport_sm_event event);
static void bfa_dport_sm_starting_qwait(struct bfa_dport_s *dport,
					enum bfa_dport_sm_event event);
static void bfa_dport_sm_starting(struct bfa_dport_s *dport,
				  enum bfa_dport_sm_event event);
static void bfa_dport_sm_dynamic_disabling(struct bfa_dport_s *dport,
				   enum bfa_dport_sm_event event);
static void bfa_dport_sm_dynamic_disabling_qwait(struct bfa_dport_s *dport,
				   enum bfa_dport_sm_event event);
static void bfa_dport_qresume(void *cbarg);
static void bfa_dport_req_comp(struct bfa_dport_s *dport,
				struct bfi_diag_dport_rsp_s *msg);
static void bfa_dport_scn(struct bfa_dport_s *dport,
				struct bfi_diag_dport_scn_s *msg);

/*
 *	BFA fcdiag module
 */
#define BFA_DIAG_QTEST_TOV	1000    /* msec */

/*
 *	Set port status to busy
 */
static void
bfa_fcdiag_set_busy_status(struct bfa_fcdiag_s *fcdiag)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(fcdiag->bfa);

	if (fcdiag->lb.lock)
		fcport->diag_busy = BFA_TRUE;
	else
		fcport->diag_busy = BFA_FALSE;
}

void
bfa_fcdiag_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_pcidev_s *pcidev)
{
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);
	struct bfa_dport_s  *dport = &fcdiag->dport;

	fcdiag->bfa             = bfa;
	fcdiag->trcmod  = bfa->trcmod;
	/* The common DIAG attach bfa_diag_attach() will do all memory claim */
	dport->bfa = bfa;
	bfa_sm_set_state(dport, bfa_dport_sm_disabled);
	bfa_reqq_winit(&dport->reqq_wait, bfa_dport_qresume, dport);
	dport->cbfn = NULL;
	dport->cbarg = NULL;
	dport->test_state = BFA_DPORT_ST_DISABLED;
	memset(&dport->result, 0, sizeof(struct bfa_diag_dport_result_s));
}

void
bfa_fcdiag_iocdisable(struct bfa_s *bfa)
{
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);
	struct bfa_dport_s *dport = &fcdiag->dport;

	bfa_trc(fcdiag, fcdiag->lb.lock);
	if (fcdiag->lb.lock) {
		fcdiag->lb.status = BFA_STATUS_IOC_FAILURE;
		fcdiag->lb.cbfn(fcdiag->lb.cbarg, fcdiag->lb.status);
		fcdiag->lb.lock = 0;
		bfa_fcdiag_set_busy_status(fcdiag);
	}

	bfa_sm_send_event(dport, BFA_DPORT_SM_HWFAIL);
}

static void
bfa_fcdiag_queuetest_timeout(void *cbarg)
{
	struct bfa_fcdiag_s       *fcdiag = cbarg;
	struct bfa_diag_qtest_result_s *res = fcdiag->qtest.result;

	bfa_trc(fcdiag, fcdiag->qtest.all);
	bfa_trc(fcdiag, fcdiag->qtest.count);

	fcdiag->qtest.timer_active = 0;

	res->status = BFA_STATUS_ETIMER;
	res->count  = QTEST_CNT_DEFAULT - fcdiag->qtest.count;
	if (fcdiag->qtest.all)
		res->queue  = fcdiag->qtest.all;

	bfa_trc(fcdiag, BFA_STATUS_ETIMER);
	fcdiag->qtest.status = BFA_STATUS_ETIMER;
	fcdiag->qtest.cbfn(fcdiag->qtest.cbarg, fcdiag->qtest.status);
	fcdiag->qtest.lock = 0;
}

static bfa_status_t
bfa_fcdiag_queuetest_send(struct bfa_fcdiag_s *fcdiag)
{
	u32	i;
	struct bfi_diag_qtest_req_s *req;

	req = bfa_reqq_next(fcdiag->bfa, fcdiag->qtest.queue);
	if (!req)
		return BFA_STATUS_DEVBUSY;

	/* build host command */
	bfi_h2i_set(req->mh, BFI_MC_DIAG, BFI_DIAG_H2I_QTEST,
		bfa_fn_lpu(fcdiag->bfa));

	for (i = 0; i < BFI_LMSG_PL_WSZ; i++)
		req->data[i] = QTEST_PAT_DEFAULT;

	bfa_trc(fcdiag, fcdiag->qtest.queue);
	/* ring door bell */
	bfa_reqq_produce(fcdiag->bfa, fcdiag->qtest.queue, req->mh);
	return BFA_STATUS_OK;
}

static void
bfa_fcdiag_queuetest_comp(struct bfa_fcdiag_s *fcdiag,
			bfi_diag_qtest_rsp_t *rsp)
{
	struct bfa_diag_qtest_result_s *res = fcdiag->qtest.result;
	bfa_status_t status = BFA_STATUS_OK;
	int i;

	/* Check timer, should still be active   */
	if (!fcdiag->qtest.timer_active) {
		bfa_trc(fcdiag, fcdiag->qtest.timer_active);
		return;
	}

	/* update count */
	fcdiag->qtest.count--;

	/* Check result */
	for (i = 0; i < BFI_LMSG_PL_WSZ; i++) {
		if (rsp->data[i] != ~(QTEST_PAT_DEFAULT)) {
			res->status = BFA_STATUS_DATACORRUPTED;
			break;
		}
	}

	if (res->status == BFA_STATUS_OK) {
		if (fcdiag->qtest.count > 0) {
			status = bfa_fcdiag_queuetest_send(fcdiag);
			if (status == BFA_STATUS_OK)
				return;
			else
				res->status = status;
		} else if (fcdiag->qtest.all > 0 &&
			fcdiag->qtest.queue < (BFI_IOC_MAX_CQS - 1)) {
			fcdiag->qtest.count = QTEST_CNT_DEFAULT;
			fcdiag->qtest.queue++;
			status = bfa_fcdiag_queuetest_send(fcdiag);
			if (status == BFA_STATUS_OK)
				return;
			else
				res->status = status;
		}
	}

	/* Stop timer when we comp all queue */
	if (fcdiag->qtest.timer_active) {
		bfa_timer_stop(&fcdiag->qtest.timer);
		fcdiag->qtest.timer_active = 0;
	}
	res->queue = fcdiag->qtest.queue;
	res->count = QTEST_CNT_DEFAULT - fcdiag->qtest.count;
	bfa_trc(fcdiag, res->count);
	bfa_trc(fcdiag, res->status);
	fcdiag->qtest.status = res->status;
	fcdiag->qtest.cbfn(fcdiag->qtest.cbarg, fcdiag->qtest.status);
	fcdiag->qtest.lock = 0;
}

static void
bfa_fcdiag_loopback_comp(struct bfa_fcdiag_s *fcdiag,
			struct bfi_diag_lb_rsp_s *rsp)
{
	struct bfa_diag_loopback_result_s *res = fcdiag->lb.result;

	res->numtxmfrm  = be32_to_cpu(rsp->res.numtxmfrm);
	res->numosffrm  = be32_to_cpu(rsp->res.numosffrm);
	res->numrcvfrm  = be32_to_cpu(rsp->res.numrcvfrm);
	res->badfrminf  = be32_to_cpu(rsp->res.badfrminf);
	res->badfrmnum  = be32_to_cpu(rsp->res.badfrmnum);
	res->status     = rsp->res.status;
	fcdiag->lb.status = rsp->res.status;
	bfa_trc(fcdiag, fcdiag->lb.status);
	fcdiag->lb.cbfn(fcdiag->lb.cbarg, fcdiag->lb.status);
	fcdiag->lb.lock = 0;
	bfa_fcdiag_set_busy_status(fcdiag);
}

static bfa_status_t
bfa_fcdiag_loopback_send(struct bfa_fcdiag_s *fcdiag,
			struct bfa_diag_loopback_s *loopback)
{
	struct bfi_diag_lb_req_s *lb_req;

	lb_req = bfa_reqq_next(fcdiag->bfa, BFA_REQQ_DIAG);
	if (!lb_req)
		return BFA_STATUS_DEVBUSY;

	/* build host command */
	bfi_h2i_set(lb_req->mh, BFI_MC_DIAG, BFI_DIAG_H2I_LOOPBACK,
		bfa_fn_lpu(fcdiag->bfa));

	lb_req->lb_mode = loopback->lb_mode;
	lb_req->speed = loopback->speed;
	lb_req->loopcnt = loopback->loopcnt;
	lb_req->pattern = loopback->pattern;

	/* ring door bell */
	bfa_reqq_produce(fcdiag->bfa, BFA_REQQ_DIAG, lb_req->mh);

	bfa_trc(fcdiag, loopback->lb_mode);
	bfa_trc(fcdiag, loopback->speed);
	bfa_trc(fcdiag, loopback->loopcnt);
	bfa_trc(fcdiag, loopback->pattern);
	return BFA_STATUS_OK;
}

/*
 *	cpe/rme intr handler
 */
void
bfa_fcdiag_intr(struct bfa_s *bfa, struct bfi_msg_s *msg)
{
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);

	switch (msg->mhdr.msg_id) {
	case BFI_DIAG_I2H_LOOPBACK:
		bfa_fcdiag_loopback_comp(fcdiag,
				(struct bfi_diag_lb_rsp_s *) msg);
		break;
	case BFI_DIAG_I2H_QTEST:
		bfa_fcdiag_queuetest_comp(fcdiag, (bfi_diag_qtest_rsp_t *)msg);
		break;
	case BFI_DIAG_I2H_DPORT:
		bfa_dport_req_comp(&fcdiag->dport,
				(struct bfi_diag_dport_rsp_s *)msg);
		break;
	case BFI_DIAG_I2H_DPORT_SCN:
		bfa_dport_scn(&fcdiag->dport,
				(struct bfi_diag_dport_scn_s *)msg);
		break;
	default:
		bfa_trc(fcdiag, msg->mhdr.msg_id);
		WARN_ON(1);
	}
}

/*
 *	Loopback test
 *
 *   @param[in] *bfa            - bfa data struct
 *   @param[in] opmode          - port operation mode
 *   @param[in] speed           - port speed
 *   @param[in] lpcnt           - loop count
 *   @param[in] pat                     - pattern to build packet
 *   @param[in] *result         - pt to bfa_diag_loopback_result_t data struct
 *   @param[in] cbfn            - callback function
 *   @param[in] cbarg           - callback functioin arg
 *
 *   @param[out]
 */
bfa_status_t
bfa_fcdiag_loopback(struct bfa_s *bfa, enum bfa_port_opmode opmode,
		enum bfa_port_speed speed, u32 lpcnt, u32 pat,
		struct bfa_diag_loopback_result_s *result, bfa_cb_diag_t cbfn,
		void *cbarg)
{
	struct  bfa_diag_loopback_s loopback;
	struct bfa_port_attr_s attr;
	bfa_status_t status;
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);

	if (!bfa_iocfc_is_operational(bfa))
		return BFA_STATUS_IOC_NON_OP;

	/* if port is PBC disabled, return error */
	if (bfa_fcport_is_pbcdisabled(bfa)) {
		bfa_trc(fcdiag, BFA_STATUS_PBC);
		return BFA_STATUS_PBC;
	}

	if (bfa_fcport_is_disabled(bfa) == BFA_FALSE) {
		bfa_trc(fcdiag, opmode);
		return BFA_STATUS_PORT_NOT_DISABLED;
	}

	/*
	 * Check if input speed is supported by the port mode
	 */
	if (bfa_ioc_get_type(&bfa->ioc) == BFA_IOC_TYPE_FC) {
		if (!(speed == BFA_PORT_SPEED_1GBPS ||
		      speed == BFA_PORT_SPEED_2GBPS ||
		      speed == BFA_PORT_SPEED_4GBPS ||
		      speed == BFA_PORT_SPEED_8GBPS ||
		      speed == BFA_PORT_SPEED_16GBPS ||
		      speed == BFA_PORT_SPEED_AUTO)) {
			bfa_trc(fcdiag, speed);
			return BFA_STATUS_UNSUPP_SPEED;
		}
		bfa_fcport_get_attr(bfa, &attr);
		bfa_trc(fcdiag, attr.speed_supported);
		if (speed > attr.speed_supported)
			return BFA_STATUS_UNSUPP_SPEED;
	} else {
		if (speed != BFA_PORT_SPEED_10GBPS) {
			bfa_trc(fcdiag, speed);
			return BFA_STATUS_UNSUPP_SPEED;
		}
	}

	/*
	 * For CT2, 1G is not supported
	 */
	if ((speed == BFA_PORT_SPEED_1GBPS) &&
	    (bfa_asic_id_ct2(bfa->ioc.pcidev.device_id))) {
		bfa_trc(fcdiag, speed);
		return BFA_STATUS_UNSUPP_SPEED;
	}

	/* For Mezz card, port speed entered needs to be checked */
	if (bfa_mfg_is_mezz(bfa->ioc.attr->card_type)) {
		if (bfa_ioc_get_type(&bfa->ioc) == BFA_IOC_TYPE_FC) {
			if (!(speed == BFA_PORT_SPEED_1GBPS ||
			      speed == BFA_PORT_SPEED_2GBPS ||
			      speed == BFA_PORT_SPEED_4GBPS ||
			      speed == BFA_PORT_SPEED_8GBPS ||
			      speed == BFA_PORT_SPEED_16GBPS ||
			      speed == BFA_PORT_SPEED_AUTO))
				return BFA_STATUS_UNSUPP_SPEED;
		} else {
			if (speed != BFA_PORT_SPEED_10GBPS)
				return BFA_STATUS_UNSUPP_SPEED;
		}
	}
	/* check to see if fcport is dport */
	if (bfa_fcport_is_dport(bfa)) {
		bfa_trc(fcdiag, fcdiag->lb.lock);
		return BFA_STATUS_DPORT_ENABLED;
	}
	/* check to see if there is another destructive diag cmd running */
	if (fcdiag->lb.lock) {
		bfa_trc(fcdiag, fcdiag->lb.lock);
		return BFA_STATUS_DEVBUSY;
	}

	fcdiag->lb.lock = 1;
	loopback.lb_mode = opmode;
	loopback.speed = speed;
	loopback.loopcnt = lpcnt;
	loopback.pattern = pat;
	fcdiag->lb.result = result;
	fcdiag->lb.cbfn = cbfn;
	fcdiag->lb.cbarg = cbarg;
	memset(result, 0, sizeof(struct bfa_diag_loopback_result_s));
	bfa_fcdiag_set_busy_status(fcdiag);

	/* Send msg to fw */
	status = bfa_fcdiag_loopback_send(fcdiag, &loopback);
	return status;
}

/*
 *	DIAG queue test command
 *
 *   @param[in] *bfa            - bfa data struct
 *   @param[in] force           - 1: don't do ioc op checking
 *   @param[in] queue           - queue no. to test
 *   @param[in] *result         - pt to bfa_diag_qtest_result_t data struct
 *   @param[in] cbfn            - callback function
 *   @param[in] *cbarg          - callback functioin arg
 *
 *   @param[out]
 */
bfa_status_t
bfa_fcdiag_queuetest(struct bfa_s *bfa, u32 force, u32 queue,
		struct bfa_diag_qtest_result_s *result, bfa_cb_diag_t cbfn,
		void *cbarg)
{
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);
	bfa_status_t status;
	bfa_trc(fcdiag, force);
	bfa_trc(fcdiag, queue);

	if (!force && !bfa_iocfc_is_operational(bfa))
		return BFA_STATUS_IOC_NON_OP;

	/* check to see if there is another destructive diag cmd running */
	if (fcdiag->qtest.lock) {
		bfa_trc(fcdiag, fcdiag->qtest.lock);
		return BFA_STATUS_DEVBUSY;
	}

	/* Initialization */
	fcdiag->qtest.lock = 1;
	fcdiag->qtest.cbfn = cbfn;
	fcdiag->qtest.cbarg = cbarg;
	fcdiag->qtest.result = result;
	fcdiag->qtest.count = QTEST_CNT_DEFAULT;

	/* Init test results */
	fcdiag->qtest.result->status = BFA_STATUS_OK;
	fcdiag->qtest.result->count  = 0;

	/* send */
	if (queue < BFI_IOC_MAX_CQS) {
		fcdiag->qtest.result->queue  = (u8)queue;
		fcdiag->qtest.queue = (u8)queue;
		fcdiag->qtest.all   = 0;
	} else {
		fcdiag->qtest.result->queue  = 0;
		fcdiag->qtest.queue = 0;
		fcdiag->qtest.all   = 1;
	}
	status = bfa_fcdiag_queuetest_send(fcdiag);

	/* Start a timer */
	if (status == BFA_STATUS_OK) {
		bfa_timer_start(bfa, &fcdiag->qtest.timer,
				bfa_fcdiag_queuetest_timeout, fcdiag,
				BFA_DIAG_QTEST_TOV);
		fcdiag->qtest.timer_active = 1;
	}
	return status;
}

/*
 * DIAG PLB is running
 *
 *   @param[in] *bfa    - bfa data struct
 *
 *   @param[out]
 */
bfa_status_t
bfa_fcdiag_lb_is_running(struct bfa_s *bfa)
{
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);
	return fcdiag->lb.lock ?  BFA_STATUS_DIAG_BUSY : BFA_STATUS_OK;
}

/*
 *	D-port
 */
#define bfa_dport_result_start(__dport, __mode) do {				\
		(__dport)->result.start_time = ktime_get_real_seconds();	\
		(__dport)->result.status = DPORT_TEST_ST_INPRG;			\
		(__dport)->result.mode = (__mode);				\
		(__dport)->result.rp_pwwn = (__dport)->rp_pwwn;			\
		(__dport)->result.rp_nwwn = (__dport)->rp_nwwn;			\
		(__dport)->result.lpcnt = (__dport)->lpcnt;			\
} while (0)

static bfa_boolean_t bfa_dport_send_req(struct bfa_dport_s *dport,
					enum bfi_dport_req req);
static void
bfa_cb_fcdiag_dport(struct bfa_dport_s *dport, bfa_status_t bfa_status)
{
	if (dport->cbfn != NULL) {
		dport->cbfn(dport->cbarg, bfa_status);
		dport->cbfn = NULL;
		dport->cbarg = NULL;
	}
}

static void
bfa_dport_sm_disabled(struct bfa_dport_s *dport, enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_ENABLE:
		bfa_fcport_dportenable(dport->bfa);
		if (bfa_dport_send_req(dport, BFI_DPORT_ENABLE))
			bfa_sm_set_state(dport, bfa_dport_sm_enabling);
		else
			bfa_sm_set_state(dport, bfa_dport_sm_enabling_qwait);
		break;

	case BFA_DPORT_SM_DISABLE:
		/* Already disabled */
		break;

	case BFA_DPORT_SM_HWFAIL:
		/* ignore */
		break;

	case BFA_DPORT_SM_SCN:
		if (dport->i2hmsg.scn.state ==  BFI_DPORT_SCN_DDPORT_ENABLE) {
			bfa_fcport_ddportenable(dport->bfa);
			dport->dynamic = BFA_TRUE;
			dport->test_state = BFA_DPORT_ST_NOTSTART;
			bfa_sm_set_state(dport, bfa_dport_sm_enabled);
		} else {
			bfa_trc(dport->bfa, dport->i2hmsg.scn.state);
			WARN_ON(1);
		}
		break;

	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static void
bfa_dport_sm_enabling_qwait(struct bfa_dport_s *dport,
			    enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_QRESUME:
		bfa_sm_set_state(dport, bfa_dport_sm_enabling);
		bfa_dport_send_req(dport, BFI_DPORT_ENABLE);
		break;

	case BFA_DPORT_SM_HWFAIL:
		bfa_reqq_wcancel(&dport->reqq_wait);
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		bfa_cb_fcdiag_dport(dport, BFA_STATUS_FAILED);
		break;

	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static void
bfa_dport_sm_enabling(struct bfa_dport_s *dport, enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_FWRSP:
		memset(&dport->result, 0,
				sizeof(struct bfa_diag_dport_result_s));
		if (dport->i2hmsg.rsp.status == BFA_STATUS_DPORT_INV_SFP) {
			dport->test_state = BFA_DPORT_ST_NO_SFP;
		} else {
			dport->test_state = BFA_DPORT_ST_INP;
			bfa_dport_result_start(dport, BFA_DPORT_OPMODE_AUTO);
		}
		bfa_sm_set_state(dport, bfa_dport_sm_enabled);
		break;

	case BFA_DPORT_SM_REQFAIL:
		dport->test_state = BFA_DPORT_ST_DISABLED;
		bfa_fcport_dportdisable(dport->bfa);
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		break;

	case BFA_DPORT_SM_HWFAIL:
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		bfa_cb_fcdiag_dport(dport, BFA_STATUS_FAILED);
		break;

	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static void
bfa_dport_sm_enabled(struct bfa_dport_s *dport, enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_START:
		if (bfa_dport_send_req(dport, BFI_DPORT_START))
			bfa_sm_set_state(dport, bfa_dport_sm_starting);
		else
			bfa_sm_set_state(dport, bfa_dport_sm_starting_qwait);
		break;

	case BFA_DPORT_SM_DISABLE:
		bfa_fcport_dportdisable(dport->bfa);
		if (bfa_dport_send_req(dport, BFI_DPORT_DISABLE))
			bfa_sm_set_state(dport, bfa_dport_sm_disabling);
		else
			bfa_sm_set_state(dport, bfa_dport_sm_disabling_qwait);
		break;

	case BFA_DPORT_SM_HWFAIL:
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		break;

	case BFA_DPORT_SM_SCN:
		switch (dport->i2hmsg.scn.state) {
		case BFI_DPORT_SCN_TESTCOMP:
			dport->test_state = BFA_DPORT_ST_COMP;
			break;

		case BFI_DPORT_SCN_TESTSTART:
			dport->test_state = BFA_DPORT_ST_INP;
			break;

		case BFI_DPORT_SCN_TESTSKIP:
		case BFI_DPORT_SCN_SUBTESTSTART:
			/* no state change */
			break;

		case BFI_DPORT_SCN_SFP_REMOVED:
			dport->test_state = BFA_DPORT_ST_NO_SFP;
			break;

		case BFI_DPORT_SCN_DDPORT_DISABLE:
			bfa_fcport_ddportdisable(dport->bfa);

			if (bfa_dport_send_req(dport, BFI_DPORT_DYN_DISABLE))
				bfa_sm_set_state(dport,
					 bfa_dport_sm_dynamic_disabling);
			else
				bfa_sm_set_state(dport,
					 bfa_dport_sm_dynamic_disabling_qwait);
			break;

		case BFI_DPORT_SCN_FCPORT_DISABLE:
			bfa_fcport_ddportdisable(dport->bfa);

			bfa_sm_set_state(dport, bfa_dport_sm_disabled);
			dport->dynamic = BFA_FALSE;
			break;

		default:
			bfa_trc(dport->bfa, dport->i2hmsg.scn.state);
			bfa_sm_fault(dport->bfa, event);
		}
		break;
	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static void
bfa_dport_sm_disabling_qwait(struct bfa_dport_s *dport,
			     enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_QRESUME:
		bfa_sm_set_state(dport, bfa_dport_sm_disabling);
		bfa_dport_send_req(dport, BFI_DPORT_DISABLE);
		break;

	case BFA_DPORT_SM_HWFAIL:
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		bfa_reqq_wcancel(&dport->reqq_wait);
		bfa_cb_fcdiag_dport(dport, BFA_STATUS_OK);
		break;

	case BFA_DPORT_SM_SCN:
		/* ignore */
		break;

	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static void
bfa_dport_sm_disabling(struct bfa_dport_s *dport, enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_FWRSP:
		dport->test_state = BFA_DPORT_ST_DISABLED;
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		break;

	case BFA_DPORT_SM_HWFAIL:
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		bfa_cb_fcdiag_dport(dport, BFA_STATUS_OK);
		break;

	case BFA_DPORT_SM_SCN:
		/* no state change */
		break;

	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static void
bfa_dport_sm_starting_qwait(struct bfa_dport_s *dport,
			    enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_QRESUME:
		bfa_sm_set_state(dport, bfa_dport_sm_starting);
		bfa_dport_send_req(dport, BFI_DPORT_START);
		break;

	case BFA_DPORT_SM_HWFAIL:
		bfa_reqq_wcancel(&dport->reqq_wait);
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		bfa_cb_fcdiag_dport(dport, BFA_STATUS_FAILED);
		break;

	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static void
bfa_dport_sm_starting(struct bfa_dport_s *dport, enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_FWRSP:
		memset(&dport->result, 0,
				sizeof(struct bfa_diag_dport_result_s));
		if (dport->i2hmsg.rsp.status == BFA_STATUS_DPORT_INV_SFP) {
			dport->test_state = BFA_DPORT_ST_NO_SFP;
		} else {
			dport->test_state = BFA_DPORT_ST_INP;
			bfa_dport_result_start(dport, BFA_DPORT_OPMODE_MANU);
		}
		/* fall thru */

	case BFA_DPORT_SM_REQFAIL:
		bfa_sm_set_state(dport, bfa_dport_sm_enabled);
		break;

	case BFA_DPORT_SM_HWFAIL:
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		bfa_cb_fcdiag_dport(dport, BFA_STATUS_FAILED);
		break;

	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static void
bfa_dport_sm_dynamic_disabling(struct bfa_dport_s *dport,
			       enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_SCN:
		switch (dport->i2hmsg.scn.state) {
		case BFI_DPORT_SCN_DDPORT_DISABLED:
			bfa_sm_set_state(dport, bfa_dport_sm_disabled);
			dport->dynamic = BFA_FALSE;
			bfa_fcport_enable(dport->bfa);
			break;

		default:
			bfa_trc(dport->bfa, dport->i2hmsg.scn.state);
			bfa_sm_fault(dport->bfa, event);

		}
		break;

	case BFA_DPORT_SM_HWFAIL:
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		bfa_cb_fcdiag_dport(dport, BFA_STATUS_OK);
		break;

	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static void
bfa_dport_sm_dynamic_disabling_qwait(struct bfa_dport_s *dport,
			    enum bfa_dport_sm_event event)
{
	bfa_trc(dport->bfa, event);

	switch (event) {
	case BFA_DPORT_SM_QRESUME:
		bfa_sm_set_state(dport, bfa_dport_sm_dynamic_disabling);
		bfa_dport_send_req(dport, BFI_DPORT_DYN_DISABLE);
		break;

	case BFA_DPORT_SM_HWFAIL:
		bfa_sm_set_state(dport, bfa_dport_sm_disabled);
		bfa_reqq_wcancel(&dport->reqq_wait);
		bfa_cb_fcdiag_dport(dport, BFA_STATUS_OK);
		break;

	case BFA_DPORT_SM_SCN:
		/* ignore */
		break;

	default:
		bfa_sm_fault(dport->bfa, event);
	}
}

static bfa_boolean_t
bfa_dport_send_req(struct bfa_dport_s *dport, enum bfi_dport_req req)
{
	struct bfi_diag_dport_req_s *m;

	/*
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(dport->bfa, BFA_REQQ_DIAG);
	if (!m) {
		bfa_reqq_wait(dport->bfa, BFA_REQQ_PORT, &dport->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_DIAG, BFI_DIAG_H2I_DPORT,
		    bfa_fn_lpu(dport->bfa));
	m->req  = req;
	if ((req == BFI_DPORT_ENABLE) || (req == BFI_DPORT_START)) {
		m->lpcnt = cpu_to_be32(dport->lpcnt);
		m->payload = cpu_to_be32(dport->payload);
	}

	/*
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(dport->bfa, BFA_REQQ_DIAG, m->mh);

	return BFA_TRUE;
}

static void
bfa_dport_qresume(void *cbarg)
{
	struct bfa_dport_s *dport = cbarg;

	bfa_sm_send_event(dport, BFA_DPORT_SM_QRESUME);
}

static void
bfa_dport_req_comp(struct bfa_dport_s *dport, struct bfi_diag_dport_rsp_s *msg)
{
	msg->status = cpu_to_be32(msg->status);
	dport->i2hmsg.rsp.status = msg->status;
	dport->rp_pwwn = msg->pwwn;
	dport->rp_nwwn = msg->nwwn;

	if ((msg->status == BFA_STATUS_OK) ||
	    (msg->status == BFA_STATUS_DPORT_NO_SFP)) {
		bfa_trc(dport->bfa, msg->status);
		bfa_trc(dport->bfa, dport->rp_pwwn);
		bfa_trc(dport->bfa, dport->rp_nwwn);
		bfa_sm_send_event(dport, BFA_DPORT_SM_FWRSP);

	} else {
		bfa_trc(dport->bfa, msg->status);
		bfa_sm_send_event(dport, BFA_DPORT_SM_REQFAIL);
	}
	bfa_cb_fcdiag_dport(dport, msg->status);
}

static bfa_boolean_t
bfa_dport_is_sending_req(struct bfa_dport_s *dport)
{
	if (bfa_sm_cmp_state(dport, bfa_dport_sm_enabling)	||
	    bfa_sm_cmp_state(dport, bfa_dport_sm_enabling_qwait) ||
	    bfa_sm_cmp_state(dport, bfa_dport_sm_disabling)	||
	    bfa_sm_cmp_state(dport, bfa_dport_sm_disabling_qwait) ||
	    bfa_sm_cmp_state(dport, bfa_dport_sm_starting)	||
	    bfa_sm_cmp_state(dport, bfa_dport_sm_starting_qwait)) {
		return BFA_TRUE;
	} else {
		return BFA_FALSE;
	}
}

static void
bfa_dport_scn(struct bfa_dport_s *dport, struct bfi_diag_dport_scn_s *msg)
{
	int i;
	uint8_t subtesttype;

	bfa_trc(dport->bfa, msg->state);
	dport->i2hmsg.scn.state = msg->state;

	switch (dport->i2hmsg.scn.state) {
	case BFI_DPORT_SCN_TESTCOMP:
		dport->result.end_time = ktime_get_real_seconds();
		bfa_trc(dport->bfa, dport->result.end_time);

		dport->result.status = msg->info.testcomp.status;
		bfa_trc(dport->bfa, dport->result.status);

		dport->result.roundtrip_latency =
			cpu_to_be32(msg->info.testcomp.latency);
		dport->result.est_cable_distance =
			cpu_to_be32(msg->info.testcomp.distance);
		dport->result.buffer_required =
			be16_to_cpu(msg->info.testcomp.numbuffer);

		dport->result.frmsz = be16_to_cpu(msg->info.testcomp.frm_sz);
		dport->result.speed = msg->info.testcomp.speed;

		bfa_trc(dport->bfa, dport->result.roundtrip_latency);
		bfa_trc(dport->bfa, dport->result.est_cable_distance);
		bfa_trc(dport->bfa, dport->result.buffer_required);
		bfa_trc(dport->bfa, dport->result.frmsz);
		bfa_trc(dport->bfa, dport->result.speed);

		for (i = DPORT_TEST_ELOOP; i < DPORT_TEST_MAX; i++) {
			dport->result.subtest[i].status =
				msg->info.testcomp.subtest_status[i];
			bfa_trc(dport->bfa, dport->result.subtest[i].status);
		}
		break;

	case BFI_DPORT_SCN_TESTSKIP:
	case BFI_DPORT_SCN_DDPORT_ENABLE:
		memset(&dport->result, 0,
				sizeof(struct bfa_diag_dport_result_s));
		break;

	case BFI_DPORT_SCN_TESTSTART:
		memset(&dport->result, 0,
				sizeof(struct bfa_diag_dport_result_s));
		dport->rp_pwwn = msg->info.teststart.pwwn;
		dport->rp_nwwn = msg->info.teststart.nwwn;
		dport->lpcnt = cpu_to_be32(msg->info.teststart.numfrm);
		bfa_dport_result_start(dport, msg->info.teststart.mode);
		break;

	case BFI_DPORT_SCN_SUBTESTSTART:
		subtesttype = msg->info.teststart.type;
		dport->result.subtest[subtesttype].start_time =
			ktime_get_real_seconds();
		dport->result.subtest[subtesttype].status =
			DPORT_TEST_ST_INPRG;

		bfa_trc(dport->bfa, subtesttype);
		bfa_trc(dport->bfa,
			dport->result.subtest[subtesttype].start_time);
		break;

	case BFI_DPORT_SCN_SFP_REMOVED:
	case BFI_DPORT_SCN_DDPORT_DISABLED:
	case BFI_DPORT_SCN_DDPORT_DISABLE:
	case BFI_DPORT_SCN_FCPORT_DISABLE:
		dport->result.status = DPORT_TEST_ST_IDLE;
		break;

	default:
		bfa_sm_fault(dport->bfa, msg->state);
	}

	bfa_sm_send_event(dport, BFA_DPORT_SM_SCN);
}

/*
 * Dport enable
 *
 * @param[in] *bfa            - bfa data struct
 */
bfa_status_t
bfa_dport_enable(struct bfa_s *bfa, u32 lpcnt, u32 pat,
				bfa_cb_diag_t cbfn, void *cbarg)
{
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);
	struct bfa_dport_s  *dport = &fcdiag->dport;

	/*
	 * Dport is not support in MEZZ card
	 */
	if (bfa_mfg_is_mezz(dport->bfa->ioc.attr->card_type)) {
		bfa_trc(dport->bfa, BFA_STATUS_PBC);
		return BFA_STATUS_CMD_NOTSUPP_MEZZ;
	}

	/*
	 * Dport is supported in CT2 or above
	 */
	if (!(bfa_asic_id_ct2(dport->bfa->ioc.pcidev.device_id))) {
		bfa_trc(dport->bfa, dport->bfa->ioc.pcidev.device_id);
		return BFA_STATUS_FEATURE_NOT_SUPPORTED;
	}

	/*
	 * Check to see if IOC is down
	*/
	if (!bfa_iocfc_is_operational(bfa))
		return BFA_STATUS_IOC_NON_OP;

	/* if port is PBC disabled, return error */
	if (bfa_fcport_is_pbcdisabled(bfa)) {
		bfa_trc(dport->bfa, BFA_STATUS_PBC);
		return BFA_STATUS_PBC;
	}

	/*
	 * Check if port mode is FC port
	 */
	if (bfa_ioc_get_type(&bfa->ioc) != BFA_IOC_TYPE_FC) {
		bfa_trc(dport->bfa, bfa_ioc_get_type(&bfa->ioc));
		return BFA_STATUS_CMD_NOTSUPP_CNA;
	}

	/*
	 * Check if port is in LOOP mode
	 */
	if ((bfa_fcport_get_cfg_topology(bfa) == BFA_PORT_TOPOLOGY_LOOP) ||
	    (bfa_fcport_get_topology(bfa) == BFA_PORT_TOPOLOGY_LOOP)) {
		bfa_trc(dport->bfa, 0);
		return BFA_STATUS_TOPOLOGY_LOOP;
	}

	/*
	 * Check if port is TRUNK mode
	 */
	if (bfa_fcport_is_trunk_enabled(bfa)) {
		bfa_trc(dport->bfa, 0);
		return BFA_STATUS_ERROR_TRUNK_ENABLED;
	}

	/*
	 * Check if diag loopback is running
	 */
	if (bfa_fcdiag_lb_is_running(bfa)) {
		bfa_trc(dport->bfa, 0);
		return BFA_STATUS_DIAG_BUSY;
	}

	/*
	 * Check to see if port is disable or in dport state
	 */
	if ((bfa_fcport_is_disabled(bfa) == BFA_FALSE) &&
	    (bfa_fcport_is_dport(bfa) == BFA_FALSE)) {
		bfa_trc(dport->bfa, 0);
		return BFA_STATUS_PORT_NOT_DISABLED;
	}

	/*
	 * Check if dport is in dynamic mode
	 */
	if (dport->dynamic)
		return BFA_STATUS_DDPORT_ERR;

	/*
	 * Check if dport is busy
	 */
	if (bfa_dport_is_sending_req(dport))
		return BFA_STATUS_DEVBUSY;

	/*
	 * Check if dport is already enabled
	 */
	if (bfa_sm_cmp_state(dport, bfa_dport_sm_enabled)) {
		bfa_trc(dport->bfa, 0);
		return BFA_STATUS_DPORT_ENABLED;
	}

	bfa_trc(dport->bfa, lpcnt);
	bfa_trc(dport->bfa, pat);
	dport->lpcnt = (lpcnt) ? lpcnt : DPORT_ENABLE_LOOPCNT_DEFAULT;
	dport->payload = (pat) ? pat : LB_PATTERN_DEFAULT;
	dport->cbfn = cbfn;
	dport->cbarg = cbarg;

	bfa_sm_send_event(dport, BFA_DPORT_SM_ENABLE);
	return BFA_STATUS_OK;
}

/*
 *	Dport disable
 *
 *	@param[in] *bfa            - bfa data struct
 */
bfa_status_t
bfa_dport_disable(struct bfa_s *bfa, bfa_cb_diag_t cbfn, void *cbarg)
{
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);
	struct bfa_dport_s *dport = &fcdiag->dport;

	if (bfa_ioc_is_disabled(&bfa->ioc))
		return BFA_STATUS_IOC_DISABLED;

	/* if port is PBC disabled, return error */
	if (bfa_fcport_is_pbcdisabled(bfa)) {
		bfa_trc(dport->bfa, BFA_STATUS_PBC);
		return BFA_STATUS_PBC;
	}

	/*
	 * Check if dport is in dynamic mode
	 */
	if (dport->dynamic) {
		return BFA_STATUS_DDPORT_ERR;
	}

	/*
	 * Check to see if port is disable or in dport state
	 */
	if ((bfa_fcport_is_disabled(bfa) == BFA_FALSE) &&
	    (bfa_fcport_is_dport(bfa) == BFA_FALSE)) {
		bfa_trc(dport->bfa, 0);
		return BFA_STATUS_PORT_NOT_DISABLED;
	}

	/*
	 * Check if dport is busy
	 */
	if (bfa_dport_is_sending_req(dport))
		return BFA_STATUS_DEVBUSY;

	/*
	 * Check if dport is already disabled
	 */
	if (bfa_sm_cmp_state(dport, bfa_dport_sm_disabled)) {
		bfa_trc(dport->bfa, 0);
		return BFA_STATUS_DPORT_DISABLED;
	}

	dport->cbfn = cbfn;
	dport->cbarg = cbarg;

	bfa_sm_send_event(dport, BFA_DPORT_SM_DISABLE);
	return BFA_STATUS_OK;
}

/*
 * Dport start -- restart dport test
 *
 *   @param[in] *bfa		- bfa data struct
 */
bfa_status_t
bfa_dport_start(struct bfa_s *bfa, u32 lpcnt, u32 pat,
			bfa_cb_diag_t cbfn, void *cbarg)
{
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);
	struct bfa_dport_s *dport = &fcdiag->dport;

	/*
	 * Check to see if IOC is down
	 */
	if (!bfa_iocfc_is_operational(bfa))
		return BFA_STATUS_IOC_NON_OP;

	/*
	 * Check if dport is in dynamic mode
	 */
	if (dport->dynamic)
		return BFA_STATUS_DDPORT_ERR;

	/*
	 * Check if dport is busy
	 */
	if (bfa_dport_is_sending_req(dport))
		return BFA_STATUS_DEVBUSY;

	/*
	 * Check if dport is in enabled state.
	 * Test can only be restart when previous test has completed
	 */
	if (!bfa_sm_cmp_state(dport, bfa_dport_sm_enabled)) {
		bfa_trc(dport->bfa, 0);
		return BFA_STATUS_DPORT_DISABLED;

	} else {
		if (dport->test_state == BFA_DPORT_ST_NO_SFP)
			return BFA_STATUS_DPORT_INV_SFP;

		if (dport->test_state == BFA_DPORT_ST_INP)
			return BFA_STATUS_DEVBUSY;

		WARN_ON(dport->test_state != BFA_DPORT_ST_COMP);
	}

	bfa_trc(dport->bfa, lpcnt);
	bfa_trc(dport->bfa, pat);

	dport->lpcnt = (lpcnt) ? lpcnt : DPORT_ENABLE_LOOPCNT_DEFAULT;
	dport->payload = (pat) ? pat : LB_PATTERN_DEFAULT;

	dport->cbfn = cbfn;
	dport->cbarg = cbarg;

	bfa_sm_send_event(dport, BFA_DPORT_SM_START);
	return BFA_STATUS_OK;
}

/*
 * Dport show -- return dport test result
 *
 *   @param[in] *bfa		- bfa data struct
 */
bfa_status_t
bfa_dport_show(struct bfa_s *bfa, struct bfa_diag_dport_result_s *result)
{
	struct bfa_fcdiag_s *fcdiag = BFA_FCDIAG_MOD(bfa);
	struct bfa_dport_s *dport = &fcdiag->dport;

	/*
	 * Check to see if IOC is down
	 */
	if (!bfa_iocfc_is_operational(bfa))
		return BFA_STATUS_IOC_NON_OP;

	/*
	 * Check if dport is busy
	 */
	if (bfa_dport_is_sending_req(dport))
		return BFA_STATUS_DEVBUSY;

	/*
	 * Check if dport is in enabled state.
	 */
	if (!bfa_sm_cmp_state(dport, bfa_dport_sm_enabled)) {
		bfa_trc(dport->bfa, 0);
		return BFA_STATUS_DPORT_DISABLED;

	}

	/*
	 * Check if there is SFP
	 */
	if (dport->test_state == BFA_DPORT_ST_NO_SFP)
		return BFA_STATUS_DPORT_INV_SFP;

	memcpy(result, &dport->result, sizeof(struct bfa_diag_dport_result_s));

	return BFA_STATUS_OK;
}
