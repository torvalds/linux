/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __BFA_FCPIM_PRIV_H__
#define __BFA_FCPIM_PRIV_H__

#include <bfa_fcpim.h>
#include <defs/bfa_defs_fcpim.h>
#include <cs/bfa_wc.h>
#include "bfa_sgpg_priv.h"

#define BFA_ITNIM_MIN   32
#define BFA_ITNIM_MAX   1024

#define BFA_IOIM_MIN    8
#define BFA_IOIM_MAX    2000

#define BFA_TSKIM_MIN   4
#define BFA_TSKIM_MAX   512
#define BFA_FCPIM_PATHTOV_DEF	(30 * 1000)	/* in millisecs */
#define BFA_FCPIM_PATHTOV_MAX	(90 * 1000)	/* in millisecs */

#define bfa_fcpim_stats(__fcpim, __stats)   \
    ((__fcpim)->stats.__stats++)

struct bfa_fcpim_mod_s {
	struct bfa_s 	*bfa;
	struct bfa_itnim_s 	*itnim_arr;
	struct bfa_ioim_s 	*ioim_arr;
	struct bfa_ioim_sp_s *ioim_sp_arr;
	struct bfa_tskim_s 	*tskim_arr;
	struct bfa_dma_s	snsbase;
	int			num_itnims;
	int			num_ioim_reqs;
	int			num_tskim_reqs;
	u32		path_tov;
	u16		q_depth;
	u16		rsvd;
	struct list_head 	itnim_q;        /*  queue of active itnim    */
	struct list_head 	ioim_free_q;    /*  free IO resources        */
	struct list_head 	ioim_resfree_q; /*  IOs waiting for f/w      */
	struct list_head 	ioim_comp_q;    /*  IO global comp Q         */
	struct list_head 	tskim_free_q;
	u32	ios_active;	/*  current active IOs	      */
	u32	delay_comp;
	struct bfa_fcpim_stats_s stats;
};

struct bfa_ioim_s;
struct bfa_tskim_s;

/**
 * BFA IO (initiator mode)
 */
struct bfa_ioim_s {
	struct list_head qe;		/*  queue elememt            */
	bfa_sm_t		sm; 	/*  BFA ioim state machine   */
	struct bfa_s 	        *bfa;	/*  BFA module               */
	struct bfa_fcpim_mod_s	*fcpim;	/*  parent fcpim module      */
	struct bfa_itnim_s 	*itnim;	/*  i-t-n nexus for this IO  */
	struct bfad_ioim_s 	*dio;	/*  driver IO handle         */
	u16	iotag;		/*  FWI IO tag               */
	u16	abort_tag;	/*  unqiue abort request tag */
	u16	nsges;		/*  number of SG elements    */
	u16	nsgpgs;		/*  number of SG pages       */
	struct bfa_sgpg_s *sgpg;	/*  first SG page            */
	struct list_head sgpg_q;		/*  allocated SG pages       */
	struct bfa_cb_qe_s hcb_qe;	/*  bfa callback qelem       */
	bfa_cb_cbfn_t io_cbfn;		/*  IO completion handler    */
	struct bfa_ioim_sp_s *iosp;	/*  slow-path IO handling    */
};

struct bfa_ioim_sp_s {
	struct bfi_msg_s 	comp_rspmsg;	/*  IO comp f/w response     */
	u8			*snsinfo;	/*  sense info for this IO   */
	struct bfa_sgpg_wqe_s sgpg_wqe;	/*  waitq elem for sgpg      */
	struct bfa_reqq_wait_s reqq_wait;	/*  to wait for room in reqq */
	bfa_boolean_t		abort_explicit;	/*  aborted by OS            */
	struct bfa_tskim_s	*tskim;		/*  Relevant TM cmd          */
};

/**
 * BFA Task management command (initiator mode)
 */
struct bfa_tskim_s {
	struct list_head          qe;
	bfa_sm_t		sm;
	struct bfa_s            *bfa;        /*  BFA module  */
	struct bfa_fcpim_mod_s  *fcpim;      /*  parent fcpim module      */
	struct bfa_itnim_s      *itnim;      /*  i-t-n nexus for this IO  */
	struct bfad_tskim_s         *dtsk;   /*  driver task mgmt cmnd    */
	bfa_boolean_t        notify;         /*  notify itnim on TM comp  */
	lun_t                lun;            /*  lun if applicable        */
	enum fcp_tm_cmnd        tm_cmnd;     /*  task management command  */
	u16             tsk_tag;        /*  FWI IO tag               */
	u8              tsecs;          /*  timeout in seconds       */
	struct bfa_reqq_wait_s  reqq_wait;   /*  to wait for room in reqq */
	struct list_head              io_q;    /*  queue of affected IOs    */
	struct bfa_wc_s             wc;      /*  waiting counter          */
	struct bfa_cb_qe_s	hcb_qe;      /*  bfa callback qelem       */
	enum bfi_tskim_status   tsk_status;  /*  TM status                */
};

/**
 * BFA i-t-n (initiator mode)
 */
struct bfa_itnim_s {
	struct list_head    qe;		/*  queue element               */
	bfa_sm_t	  sm;		/*  i-t-n im BFA state machine  */
	struct bfa_s      *bfa;		/*  bfa instance                */
	struct bfa_rport_s *rport;	/*  bfa rport                   */
	void           *ditn;		/*  driver i-t-n structure      */
	struct bfi_mhdr_s      mhdr;	/*  pre-built mhdr              */
	u8         msg_no;		/*  itnim/rport firmware handle */
	u8         reqq;		/*  CQ for requests             */
	struct bfa_cb_qe_s    hcb_qe;	/*  bfa callback qelem          */
	struct list_head pending_q;	/*  queue of pending IO requests*/
	struct list_head io_q;		/*  queue of active IO requests */
	struct list_head io_cleanup_q;	/*  IO being cleaned up         */
	struct list_head tsk_q;		/*  queue of active TM commands */
	struct list_head  delay_comp_q;/*  queue of failed inflight cmds */
	bfa_boolean_t   seq_rec;	/*  SQER supported              */
	bfa_boolean_t   is_online;	/*  itnim is ONLINE for IO      */
	bfa_boolean_t   iotov_active;	/*  IO TOV timer is active	 */
	struct bfa_wc_s        wc;	/*  waiting counter             */
	struct bfa_timer_s timer;	/*  pending IO TOV		 */
	struct bfa_reqq_wait_s reqq_wait; /*  to wait for room in reqq */
	struct bfa_fcpim_mod_s *fcpim;	/*  fcpim module                */
	struct bfa_itnim_hal_stats_s	stats;
};

#define bfa_itnim_is_online(_itnim) ((_itnim)->is_online)
#define BFA_FCPIM_MOD(_hal) (&(_hal)->modules.fcpim_mod)
#define BFA_IOIM_FROM_TAG(_fcpim, _iotag)	\
	(&fcpim->ioim_arr[_iotag])
#define BFA_TSKIM_FROM_TAG(_fcpim, _tmtag)                  \
    (&fcpim->tskim_arr[_tmtag & (fcpim->num_tskim_reqs - 1)])

/*
 * function prototypes
 */
void            bfa_ioim_attach(struct bfa_fcpim_mod_s *fcpim,
				    struct bfa_meminfo_s *minfo);
void            bfa_ioim_detach(struct bfa_fcpim_mod_s *fcpim);
void            bfa_ioim_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);
void            bfa_ioim_good_comp_isr(struct bfa_s *bfa,
					struct bfi_msg_s *msg);
void            bfa_ioim_cleanup(struct bfa_ioim_s *ioim);
void            bfa_ioim_cleanup_tm(struct bfa_ioim_s *ioim,
					struct bfa_tskim_s *tskim);
void            bfa_ioim_iocdisable(struct bfa_ioim_s *ioim);
void            bfa_ioim_tov(struct bfa_ioim_s *ioim);

void            bfa_tskim_attach(struct bfa_fcpim_mod_s *fcpim,
				     struct bfa_meminfo_s *minfo);
void            bfa_tskim_detach(struct bfa_fcpim_mod_s *fcpim);
void            bfa_tskim_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);
void            bfa_tskim_iodone(struct bfa_tskim_s *tskim);
void            bfa_tskim_iocdisable(struct bfa_tskim_s *tskim);
void            bfa_tskim_cleanup(struct bfa_tskim_s *tskim);

void            bfa_itnim_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len,
				      u32 *dm_len);
void            bfa_itnim_attach(struct bfa_fcpim_mod_s *fcpim,
				     struct bfa_meminfo_s *minfo);
void            bfa_itnim_detach(struct bfa_fcpim_mod_s *fcpim);
void            bfa_itnim_iocdisable(struct bfa_itnim_s *itnim);
void            bfa_itnim_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);
void            bfa_itnim_iodone(struct bfa_itnim_s *itnim);
void            bfa_itnim_tskdone(struct bfa_itnim_s *itnim);
bfa_boolean_t   bfa_itnim_hold_io(struct bfa_itnim_s *itnim);

#endif /* __BFA_FCPIM_PRIV_H__ */

