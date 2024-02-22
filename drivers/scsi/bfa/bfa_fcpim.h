/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
 */

#ifndef __BFA_FCPIM_H__
#define __BFA_FCPIM_H__

#include "bfa.h"
#include "bfa_svc.h"
#include "bfi_ms.h"
#include "bfa_defs_svc.h"
#include "bfa_cs.h"

/* FCP module related definitions */
#define BFA_IO_MAX	BFI_IO_MAX
#define BFA_FWTIO_MAX	2000

struct bfa_fcp_mod_s;
struct bfa_iotag_s {
	struct list_head	qe;	/* queue element	*/
	u16	tag;			/* FW IO tag		*/
};

struct bfa_itn_s {
	bfa_isr_func_t isr;
};

void bfa_itn_create(struct bfa_s *bfa, struct bfa_rport_s *rport,
		void (*isr)(struct bfa_s *bfa, struct bfi_msg_s *m));
void bfa_itn_isr(struct bfa_s *bfa, struct bfi_msg_s *m);
void bfa_iotag_attach(struct bfa_fcp_mod_s *fcp);
void bfa_fcp_res_recfg(struct bfa_s *bfa, u16 num_ioim_fw, u16 max_ioim_fw);

#define BFA_FCP_MOD(_hal)	(&(_hal)->modules.fcp_mod)
#define BFA_MEM_FCP_KVA(__bfa)	(&(BFA_FCP_MOD(__bfa)->kva_seg))
#define BFA_IOTAG_FROM_TAG(_fcp, _tag)	\
	(&(_fcp)->iotag_arr[(_tag & BFA_IOIM_IOTAG_MASK)])
#define BFA_ITN_FROM_TAG(_fcp, _tag)	\
	((_fcp)->itn_arr + ((_tag) & ((_fcp)->num_itns - 1)))
#define BFA_SNSINFO_FROM_TAG(_fcp, _tag) \
	bfa_mem_get_dmabuf_kva(_fcp, (_tag & BFA_IOIM_IOTAG_MASK),	\
	BFI_IOIM_SNSLEN)


#define BFA_ITNIM_MIN   32
#define BFA_ITNIM_MAX   1024

#define BFA_IOIM_MIN	8
#define BFA_IOIM_MAX	2000

#define BFA_TSKIM_MIN   4
#define BFA_TSKIM_MAX   512
#define BFA_FCPIM_PATHTOV_DEF	(30 * 1000)	/* in millisecs */
#define BFA_FCPIM_PATHTOV_MAX	(90 * 1000)	/* in millisecs */


#define bfa_itnim_ioprofile_update(__itnim, __index)			\
	(__itnim->ioprofile.iocomps[__index]++)

#define BFA_IOIM_RETRY_TAG_OFFSET 11
#define BFA_IOIM_IOTAG_MASK 0x07ff /* 2K IOs */
#define BFA_IOIM_RETRY_MAX 7

/* Buckets are are 512 bytes to 2MB */
static inline u32
bfa_ioim_get_index(u32 n) {
	int pos = 0;
	if (n >= (1UL)<<22)
		return BFA_IOBUCKET_MAX - 1;
	n >>= 8;
	if (n >= (1UL)<<16) {
		n >>= 16;
		pos += 16;
	}
	if (n >= 1 << 8) {
		n >>= 8;
		pos += 8;
	}
	if (n >= 1 << 4) {
		n >>= 4;
		pos += 4;
	}
	if (n >= 1 << 2) {
		n >>= 2;
		pos += 2;
	}
	if (n >= 1 << 1)
		pos += 1;

	return (n == 0) ? (0) : pos;
}

/*
 * forward declarations
 */
struct bfa_ioim_s;
struct bfa_tskim_s;
struct bfad_ioim_s;
struct bfad_tskim_s;

typedef void    (*bfa_fcpim_profile_t) (struct bfa_ioim_s *ioim);

struct bfa_fcpim_s {
	struct bfa_s		*bfa;
	struct bfa_fcp_mod_s	*fcp;
	struct bfa_itnim_s	*itnim_arr;
	struct bfa_ioim_s	*ioim_arr;
	struct bfa_ioim_sp_s	*ioim_sp_arr;
	struct bfa_tskim_s	*tskim_arr;
	int			num_itnims;
	int			num_tskim_reqs;
	u32			path_tov;
	u16			q_depth;
	u8			reqq;		/*  Request queue to be used */
	struct list_head	itnim_q;	/*  queue of active itnim */
	struct list_head	ioim_resfree_q; /*  IOs waiting for f/w */
	struct list_head	ioim_comp_q;	/*  IO global comp Q	*/
	struct list_head	tskim_free_q;
	struct list_head	tskim_unused_q;	/* Unused tskim Q */
	u32			ios_active;	/*  current active IOs	*/
	u32			delay_comp;
	struct bfa_fcpim_del_itn_stats_s del_itn_stats;
	bfa_boolean_t		ioredirect;
	bfa_boolean_t		io_profile;
	time64_t		io_profile_start_time;
	bfa_fcpim_profile_t     profile_comp;
	bfa_fcpim_profile_t     profile_start;
};

/* Max FCP dma segs required */
#define BFA_FCP_DMA_SEGS	BFI_IOIM_SNSBUF_SEGS

struct bfa_fcp_mod_s {
	struct bfa_s		*bfa;
	struct list_head	iotag_ioim_free_q;	/* free IO resources */
	struct list_head	iotag_tio_free_q;	/* free IO resources */
	struct list_head	iotag_unused_q;	/* unused IO resources*/
	struct bfa_iotag_s	*iotag_arr;
	struct bfa_itn_s	*itn_arr;
	int			max_ioim_reqs;
	int			num_ioim_reqs;
	int			num_fwtio_reqs;
	int			num_itns;
	struct bfa_dma_s	snsbase[BFA_FCP_DMA_SEGS];
	struct bfa_fcpim_s	fcpim;
	struct bfa_mem_dma_s	dma_seg[BFA_FCP_DMA_SEGS];
	struct bfa_mem_kva_s	kva_seg;
	int			throttle_update_required;
};

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

struct bfa_ioim_s;
typedef void (*bfa_ioim_sm_t)(struct bfa_ioim_s *, enum bfa_ioim_event);

/*
 * BFA IO (initiator mode)
 */
struct bfa_ioim_s {
	struct list_head	qe;		/*  queue elememt	*/
	bfa_ioim_sm_t		sm;		/*  BFA ioim state machine */
	struct bfa_s		*bfa;		/*  BFA module	*/
	struct bfa_fcpim_s	*fcpim;		/*  parent fcpim module */
	struct bfa_itnim_s	*itnim;		/*  i-t-n nexus for this IO  */
	struct bfad_ioim_s	*dio;		/*  driver IO handle	*/
	u16			iotag;		/*  FWI IO tag	*/
	u16			abort_tag;	/*  unqiue abort request tag */
	u16			nsges;		/*  number of SG elements */
	u16			nsgpgs;		/*  number of SG pages	*/
	struct bfa_sgpg_s	*sgpg;		/*  first SG page	*/
	struct list_head	sgpg_q;		/*  allocated SG pages	*/
	struct bfa_cb_qe_s	hcb_qe;		/*  bfa callback qelem	*/
	bfa_cb_cbfn_t		io_cbfn;	/*  IO completion handler */
	struct bfa_ioim_sp_s	*iosp;		/*  slow-path IO handling */
	u8			reqq;		/*  Request queue for I/O */
	u8			mode;		/*  IO is passthrough or not */
	u64			start_time;	/*  IO's Profile start val */
};

struct bfa_ioim_sp_s {
	struct bfi_msg_s	comp_rspmsg;	/*  IO comp f/w response */
	struct bfa_sgpg_wqe_s	sgpg_wqe;	/*  waitq elem for sgpg	*/
	struct bfa_reqq_wait_s	reqq_wait;	/*  to wait for room in reqq */
	bfa_boolean_t		abort_explicit;	/*  aborted by OS	*/
	struct bfa_tskim_s	*tskim;		/*  Relevant TM cmd	*/
};

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

struct bfa_tskim_s;
typedef void (*bfa_tskim_sm_t)(struct bfa_tskim_s *, enum bfa_tskim_event);

/*
 * BFA Task management command (initiator mode)
 */
struct bfa_tskim_s {
	struct list_head	qe;
	bfa_tskim_sm_t		sm;
	struct bfa_s		*bfa;	/*  BFA module  */
	struct bfa_fcpim_s	*fcpim;	/*  parent fcpim module	*/
	struct bfa_itnim_s	*itnim;	/*  i-t-n nexus for this IO  */
	struct bfad_tskim_s	*dtsk;  /*  driver task mgmt cmnd	*/
	bfa_boolean_t		notify;	/*  notify itnim on TM comp  */
	struct scsi_lun		lun;	/*  lun if applicable	*/
	enum fcp_tm_cmnd	tm_cmnd; /*  task management command  */
	u16			tsk_tag; /*  FWI IO tag	*/
	u8			tsecs;	/*  timeout in seconds	*/
	struct bfa_reqq_wait_s  reqq_wait;   /*  to wait for room in reqq */
	struct list_head	io_q;	/*  queue of affected IOs	*/
	struct bfa_wc_s		wc;	/*  waiting counter	*/
	struct bfa_cb_qe_s	hcb_qe;	/*  bfa callback qelem	*/
	enum bfi_tskim_status   tsk_status;  /*  TM status	*/
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

struct bfa_itnim_s;
typedef void (*bfa_itnim_sm_t)(struct bfa_itnim_s *, enum bfa_itnim_event);

/*
 * BFA i-t-n (initiator mode)
 */
struct bfa_itnim_s {
	struct list_head	qe;	/*  queue element	*/
	bfa_itnim_sm_t		sm;	/*  i-t-n im BFA state machine  */
	struct bfa_s		*bfa;	/*  bfa instance	*/
	struct bfa_rport_s	*rport;	/*  bfa rport	*/
	void			*ditn;	/*  driver i-t-n structure	*/
	struct bfi_mhdr_s	mhdr;	/*  pre-built mhdr	*/
	u8			msg_no;	/*  itnim/rport firmware handle */
	u8			reqq;	/*  CQ for requests	*/
	struct bfa_cb_qe_s	hcb_qe;	/*  bfa callback qelem	*/
	struct list_head pending_q;	/*  queue of pending IO requests */
	struct list_head io_q;		/*  queue of active IO requests */
	struct list_head io_cleanup_q;	/*  IO being cleaned up	*/
	struct list_head tsk_q;		/*  queue of active TM commands */
	struct list_head  delay_comp_q; /*  queue of failed inflight cmds */
	bfa_boolean_t   seq_rec;	/*  SQER supported	*/
	bfa_boolean_t   is_online;	/*  itnim is ONLINE for IO	*/
	bfa_boolean_t   iotov_active;	/*  IO TOV timer is active	 */
	struct bfa_wc_s	wc;		/*  waiting counter	*/
	struct bfa_timer_s timer;	/*  pending IO TOV	 */
	struct bfa_reqq_wait_s reqq_wait; /*  to wait for room in reqq */
	struct bfa_fcpim_s *fcpim;	/*  fcpim module	*/
	struct bfa_itnim_iostats_s	stats;
	struct bfa_itnim_ioprofile_s  ioprofile;
};

#define bfa_itnim_is_online(_itnim) ((_itnim)->is_online)
#define BFA_FCPIM(_hal)	(&(_hal)->modules.fcp_mod.fcpim)
#define BFA_IOIM_TAG_2_ID(_iotag)	((_iotag) & BFA_IOIM_IOTAG_MASK)
#define BFA_IOIM_FROM_TAG(_fcpim, _iotag)	\
	(&fcpim->ioim_arr[(_iotag & BFA_IOIM_IOTAG_MASK)])
#define BFA_TSKIM_FROM_TAG(_fcpim, _tmtag)	\
	(&fcpim->tskim_arr[_tmtag & (fcpim->num_tskim_reqs - 1)])

#define bfa_io_profile_start_time(_bfa)	\
	((_bfa)->modules.fcp_mod.fcpim.io_profile_start_time)
#define bfa_fcpim_get_io_profile(_bfa)	\
	((_bfa)->modules.fcp_mod.fcpim.io_profile)
#define bfa_ioim_update_iotag(__ioim) do {				\
	uint16_t k = (__ioim)->iotag >> BFA_IOIM_RETRY_TAG_OFFSET;	\
	k++; (__ioim)->iotag &= BFA_IOIM_IOTAG_MASK;			\
	(__ioim)->iotag |= k << BFA_IOIM_RETRY_TAG_OFFSET;		\
} while (0)

static inline bfa_boolean_t
bfa_ioim_maxretry_reached(struct bfa_ioim_s *ioim)
{
	uint16_t k = ioim->iotag >> BFA_IOIM_RETRY_TAG_OFFSET;
	if (k < BFA_IOIM_RETRY_MAX)
		return BFA_FALSE;
	return BFA_TRUE;
}

/*
 * function prototypes
 */
void	bfa_ioim_attach(struct bfa_fcpim_s *fcpim);
void	bfa_ioim_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);
void	bfa_ioim_good_comp_isr(struct bfa_s *bfa,
					struct bfi_msg_s *msg);
void	bfa_ioim_cleanup(struct bfa_ioim_s *ioim);
void	bfa_ioim_cleanup_tm(struct bfa_ioim_s *ioim,
					struct bfa_tskim_s *tskim);
void	bfa_ioim_iocdisable(struct bfa_ioim_s *ioim);
void	bfa_ioim_tov(struct bfa_ioim_s *ioim);

void	bfa_tskim_attach(struct bfa_fcpim_s *fcpim);
void	bfa_tskim_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);
void	bfa_tskim_iodone(struct bfa_tskim_s *tskim);
void	bfa_tskim_iocdisable(struct bfa_tskim_s *tskim);
void	bfa_tskim_cleanup(struct bfa_tskim_s *tskim);
void	bfa_tskim_res_recfg(struct bfa_s *bfa, u16 num_tskim_fw);

void	bfa_itnim_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len);
void	bfa_itnim_attach(struct bfa_fcpim_s *fcpim);
void	bfa_itnim_iocdisable(struct bfa_itnim_s *itnim);
void	bfa_itnim_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);
void	bfa_itnim_iodone(struct bfa_itnim_s *itnim);
void	bfa_itnim_tskdone(struct bfa_itnim_s *itnim);
bfa_boolean_t   bfa_itnim_hold_io(struct bfa_itnim_s *itnim);

/*
 * bfa fcpim module API functions
 */
void	bfa_fcpim_path_tov_set(struct bfa_s *bfa, u16 path_tov);
u16	bfa_fcpim_path_tov_get(struct bfa_s *bfa);
u16	bfa_fcpim_qdepth_get(struct bfa_s *bfa);
bfa_status_t bfa_fcpim_port_iostats(struct bfa_s *bfa,
			struct bfa_itnim_iostats_s *stats, u8 lp_tag);
void bfa_fcpim_add_stats(struct bfa_itnim_iostats_s *fcpim_stats,
			struct bfa_itnim_iostats_s *itnim_stats);
bfa_status_t bfa_fcpim_profile_on(struct bfa_s *bfa, time64_t time);
bfa_status_t bfa_fcpim_profile_off(struct bfa_s *bfa);

#define bfa_fcpim_ioredirect_enabled(__bfa)				\
	(((struct bfa_fcpim_s *)(BFA_FCPIM(__bfa)))->ioredirect)

#define bfa_fcpim_get_next_reqq(__bfa, __qid)				\
{									\
	struct bfa_fcpim_s *__fcpim = BFA_FCPIM(__bfa);      \
	__fcpim->reqq++;						\
	__fcpim->reqq &= (BFI_IOC_MAX_CQS - 1);      \
	*(__qid) = __fcpim->reqq;					\
}

#define bfa_iocfc_map_msg_to_qid(__msg, __qid)				\
	*(__qid) = (u8)((__msg) & (BFI_IOC_MAX_CQS - 1));
/*
 * bfa itnim API functions
 */
struct bfa_itnim_s *bfa_itnim_create(struct bfa_s *bfa,
		struct bfa_rport_s *rport, void *itnim);
void bfa_itnim_delete(struct bfa_itnim_s *itnim);
void bfa_itnim_online(struct bfa_itnim_s *itnim, bfa_boolean_t seq_rec);
void bfa_itnim_offline(struct bfa_itnim_s *itnim);
void bfa_itnim_clear_stats(struct bfa_itnim_s *itnim);
bfa_status_t bfa_itnim_get_ioprofile(struct bfa_itnim_s *itnim,
			struct bfa_itnim_ioprofile_s *ioprofile);

#define bfa_itnim_get_reqq(__ioim) (((struct bfa_ioim_s *)__ioim)->itnim->reqq)

/*
 * BFA completion callback for bfa_itnim_online().
 */
void	bfa_cb_itnim_online(void *itnim);

/*
 * BFA completion callback for bfa_itnim_offline().
 */
void	bfa_cb_itnim_offline(void *itnim);
void	bfa_cb_itnim_tov_begin(void *itnim);
void	bfa_cb_itnim_tov(void *itnim);

/*
 * BFA notification to FCS/driver for second level error recovery.
 * Atleast one I/O request has timedout and target is unresponsive to
 * repeated abort requests. Second level error recovery should be initiated
 * by starting implicit logout and recovery procedures.
 */
void	bfa_cb_itnim_sler(void *itnim);

/*
 * bfa ioim API functions
 */
struct bfa_ioim_s	*bfa_ioim_alloc(struct bfa_s *bfa,
					struct bfad_ioim_s *dio,
					struct bfa_itnim_s *itnim,
					u16 nsgles);

void		bfa_ioim_free(struct bfa_ioim_s *ioim);
void		bfa_ioim_start(struct bfa_ioim_s *ioim);
bfa_status_t	bfa_ioim_abort(struct bfa_ioim_s *ioim);
void		bfa_ioim_delayed_comp(struct bfa_ioim_s *ioim,
				      bfa_boolean_t iotov);
/*
 * I/O completion notification.
 *
 * @param[in]		dio			driver IO structure
 * @param[in]		io_status		IO completion status
 * @param[in]		scsi_status		SCSI status returned by target
 * @param[in]		sns_len			SCSI sense length, 0 if none
 * @param[in]		sns_info		SCSI sense data, if any
 * @param[in]		residue			Residual length
 *
 * @return None
 */
void bfa_cb_ioim_done(void *bfad, struct bfad_ioim_s *dio,
			enum bfi_ioim_status io_status,
			u8 scsi_status, int sns_len,
			u8 *sns_info, s32 residue);

/*
 * I/O good completion notification.
 */
void bfa_cb_ioim_good_comp(void *bfad, struct bfad_ioim_s *dio);

/*
 * I/O abort completion notification
 */
void bfa_cb_ioim_abort(void *bfad, struct bfad_ioim_s *dio);

/*
 * bfa tskim API functions
 */
struct bfa_tskim_s *bfa_tskim_alloc(struct bfa_s *bfa,
			struct bfad_tskim_s *dtsk);
void bfa_tskim_free(struct bfa_tskim_s *tskim);
void bfa_tskim_start(struct bfa_tskim_s *tskim,
			struct bfa_itnim_s *itnim, struct scsi_lun lun,
			enum fcp_tm_cmnd tm, u8 t_secs);
void bfa_cb_tskim_done(void *bfad, struct bfad_tskim_s *dtsk,
			enum bfi_tskim_status tsk_status);

void	bfa_fcpim_lunmask_rp_update(struct bfa_s *bfa, wwn_t lp_wwn,
			wwn_t rp_wwn, u16 rp_tag, u8 lp_tag);
bfa_status_t	bfa_fcpim_lunmask_update(struct bfa_s *bfa, u32 on_off);
bfa_status_t	bfa_fcpim_lunmask_query(struct bfa_s *bfa, void *buf);
bfa_status_t	bfa_fcpim_lunmask_delete(struct bfa_s *bfa, u16 vf_id,
				wwn_t *pwwn, wwn_t rpwwn, struct scsi_lun lun);
bfa_status_t	bfa_fcpim_lunmask_add(struct bfa_s *bfa, u16 vf_id,
				wwn_t *pwwn, wwn_t rpwwn, struct scsi_lun lun);
bfa_status_t	bfa_fcpim_lunmask_clear(struct bfa_s *bfa);
u16		bfa_fcpim_read_throttle(struct bfa_s *bfa);
bfa_status_t	bfa_fcpim_write_throttle(struct bfa_s *bfa, u16 value);
bfa_status_t	bfa_fcpim_throttle_set(struct bfa_s *bfa, u16 value);
bfa_status_t	bfa_fcpim_throttle_get(struct bfa_s *bfa, void *buf);
u16     bfa_fcpim_get_throttle_cfg(struct bfa_s *bfa, u16 drv_cfg_param);

#endif /* __BFA_FCPIM_H__ */
