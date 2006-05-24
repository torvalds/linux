/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2006 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#define FC_MAX_HOLD_RSCN     32	      /* max number of deferred RSCNs */
#define FC_MAX_NS_RSP        65536    /* max size NameServer rsp */
#define FC_MAXLOOP           126      /* max devices supported on a fc loop */
#define LPFC_DISC_FLOGI_TMO  10	      /* Discovery FLOGI ratov */


/* This is the protocol dependent definition for a Node List Entry.
 * This is used by Fibre Channel protocol to support FCP.
 */

/* worker thread events */
enum lpfc_work_type {
	LPFC_EVT_NODEV_TMO,
	LPFC_EVT_ONLINE,
	LPFC_EVT_OFFLINE,
	LPFC_EVT_WARM_START,
	LPFC_EVT_KILL,
	LPFC_EVT_ELS_RETRY,
};

/* structure used to queue event to the discovery tasklet */
struct lpfc_work_evt {
	struct list_head      evt_listp;
	void                * evt_arg1;
	void                * evt_arg2;
	enum lpfc_work_type   evt;
};


struct lpfc_nodelist {
	struct list_head nlp_listp;
	struct lpfc_name nlp_portname;		/* port name */
	struct lpfc_name nlp_nodename;		/* node name */
	uint32_t         nlp_flag;		/* entry  flags */
	uint32_t         nlp_DID;		/* FC D_ID of entry */
	uint32_t         nlp_last_elscmd;	/* Last ELS cmd sent */
	uint16_t         nlp_type;
#define NLP_FC_NODE        0x1			/* entry is an FC node */
#define NLP_FABRIC         0x4			/* entry rep a Fabric entity */
#define NLP_FCP_TARGET     0x8			/* entry is an FCP target */
#define NLP_FCP_INITIATOR  0x10			/* entry is an FCP Initiator */

	uint16_t        nlp_rpi;
	uint16_t        nlp_state;		/* state transition indicator */
	uint16_t        nlp_prev_state;		/* state transition indicator */
	uint16_t        nlp_xri;		/* output exchange id for RPI */
	uint16_t        nlp_sid;		/* scsi id */
#define NLP_NO_SID		0xffff
	uint16_t	nlp_maxframe;		/* Max RCV frame size */
	uint8_t		nlp_class_sup;		/* Supported Classes */
	uint8_t         nlp_retry;		/* used for ELS retries */
	uint8_t         nlp_disc_refcnt;	/* used for DSM */
	uint8_t         nlp_fcp_info;	        /* class info, bits 0-3 */
#define NLP_FCP_2_DEVICE   0x10			/* FCP-2 device */

	struct timer_list   nlp_delayfunc;	/* Used for delayed ELS cmds */
	struct timer_list   nlp_tmofunc;	/* Used for nodev tmo */
	struct fc_rport *rport;			/* Corresponding FC transport
						   port structure */
	struct lpfc_hba      *nlp_phba;
	struct lpfc_work_evt nodev_timeout_evt;
	struct lpfc_work_evt els_retry_evt;
	unsigned long last_ramp_up_time;        /* jiffy of last ramp up */
	unsigned long last_q_full_time;		/* jiffy of last queue full */
};

/* Defines for nlp_flag (uint32) */
#define NLP_NO_LIST        0x0		/* Indicates immediately free node */
#define NLP_UNUSED_LIST    0x1		/* Flg to indicate node will be freed */
#define NLP_PLOGI_LIST     0x2		/* Flg to indicate sent PLOGI */
#define NLP_ADISC_LIST     0x3		/* Flg to indicate sent ADISC */
#define NLP_REGLOGIN_LIST  0x4		/* Flg to indicate sent REG_LOGIN */
#define NLP_PRLI_LIST      0x5		/* Flg to indicate sent PRLI */
#define NLP_UNMAPPED_LIST  0x6		/* Node is now unmapped */
#define NLP_MAPPED_LIST    0x7		/* Node is now mapped */
#define NLP_NPR_LIST       0x8		/* Node is in NPort Recovery state */
#define NLP_JUST_DQ        0x9		/* just deque ndlp in lpfc_nlp_list */
#define NLP_LIST_MASK      0xf		/* mask to see what list node is on */
#define NLP_PLOGI_SND      0x20		/* sent PLOGI request for this entry */
#define NLP_PRLI_SND       0x40		/* sent PRLI request for this entry */
#define NLP_ADISC_SND      0x80		/* sent ADISC request for this entry */
#define NLP_LOGO_SND       0x100	/* sent LOGO request for this entry */
#define NLP_RNID_SND       0x400	/* sent RNID request for this entry */
#define NLP_ELS_SND_MASK   0x7e0	/* sent ELS request for this entry */
#define NLP_NODEV_TMO      0x10000	/* nodev timeout is running for node */
#define NLP_DELAY_TMO      0x20000	/* delay timeout is running for node */
#define NLP_NPR_2B_DISC    0x40000	/* node is included in num_disc_nodes */
#define NLP_RCV_PLOGI      0x80000	/* Rcv'ed PLOGI from remote system */
#define NLP_LOGO_ACC       0x100000	/* Process LOGO after ACC completes */
#define NLP_TGT_NO_SCSIID  0x200000	/* good PRLI but no binding for scsid */
#define NLP_ACC_REGLOGIN   0x1000000	/* Issue Reg Login after successful
					   ACC */
#define NLP_NPR_ADISC      0x2000000	/* Issue ADISC when dq'ed from
					   NPR list */
#define NLP_DELAY_REMOVE   0x4000000	/* Defer removal till end of DSM */
#define NLP_NODEV_REMOVE   0x8000000	/* Defer removal till discovery ends */

/* Defines for list searchs */
#define NLP_SEARCH_MAPPED    0x1	/* search mapped */
#define NLP_SEARCH_UNMAPPED  0x2	/* search unmapped */
#define NLP_SEARCH_PLOGI     0x4	/* search plogi */
#define NLP_SEARCH_ADISC     0x8	/* search adisc */
#define NLP_SEARCH_REGLOGIN  0x10	/* search reglogin */
#define NLP_SEARCH_PRLI      0x20	/* search prli */
#define NLP_SEARCH_NPR       0x40	/* search npr */
#define NLP_SEARCH_UNUSED    0x80	/* search mapped */
#define NLP_SEARCH_ALL       0xff	/* search all lists */

/* There are 4 different double linked lists nodelist entries can reside on.
 * The Port Login (PLOGI) list and Address Discovery (ADISC) list are used
 * when Link Up discovery or Registered State Change Notification (RSCN)
 * processing is needed.  Each list holds the nodes that require a PLOGI or
 * ADISC Extended Link Service (ELS) request.  These lists keep track of the
 * nodes affected by an RSCN, or a Link Up (Typically, all nodes are effected
 * by Link Up) event.  The unmapped_list contains all nodes that have
 * successfully logged into at the Fibre Channel level.  The
 * mapped_list will contain all nodes that are mapped FCP targets.
 *
 * The bind list is a list of undiscovered (potentially non-existent) nodes
 * that we have saved binding information on. This information is used when
 * nodes transition from the unmapped to the mapped list.
 */

/* Defines for nlp_state */
#define NLP_STE_UNUSED_NODE       0x0	/* node is just allocated */
#define NLP_STE_PLOGI_ISSUE       0x1	/* PLOGI was sent to NL_PORT */
#define NLP_STE_ADISC_ISSUE       0x2	/* ADISC was sent to NL_PORT */
#define NLP_STE_REG_LOGIN_ISSUE   0x3	/* REG_LOGIN was issued for NL_PORT */
#define NLP_STE_PRLI_ISSUE        0x4	/* PRLI was sent to NL_PORT */
#define NLP_STE_UNMAPPED_NODE     0x5	/* PRLI completed from NL_PORT */
#define NLP_STE_MAPPED_NODE       0x6	/* Identified as a FCP Target */
#define NLP_STE_NPR_NODE          0x7	/* NPort disappeared */
#define NLP_STE_MAX_STATE         0x8
#define NLP_STE_FREED_NODE        0xff	/* node entry was freed to MEM_NLP */

/* For UNUSED_NODE state, the node has just been allocated.
 * For PLOGI_ISSUE and REG_LOGIN_ISSUE, the node is on
 * the PLOGI list. For REG_LOGIN_COMPL, the node is taken off the PLOGI list
 * and put on the unmapped list. For ADISC processing, the node is taken off
 * the ADISC list and placed on either the mapped or unmapped list (depending
 * on its previous state). Once on the unmapped list, a PRLI is issued and the
 * state changed to PRLI_ISSUE. When the PRLI completion occurs, the state is
 * changed to PRLI_COMPL. If the completion indicates a mapped
 * node, the node is taken off the unmapped list. The binding list is checked
 * for a valid binding, or a binding is automatically assigned. If binding
 * assignment is unsuccessful, the node is left on the unmapped list. If
 * binding assignment is successful, the associated binding list entry (if
 * any) is removed, and the node is placed on the mapped list.
 */
/*
 * For a Link Down, all nodes on the ADISC, PLOGI, unmapped or mapped
 * lists will receive a DEVICE_RECOVERY event. If the linkdown or nodev timers
 * expire, all effected nodes will receive a DEVICE_RM event.
 */
/*
 * For a Link Up or RSCN, all nodes will move from the mapped / unmapped lists
 * to either the ADISC or PLOGI list.  After a Nameserver query or ALPA loopmap
 * check, additional nodes may be added (DEVICE_ADD) or removed (DEVICE_RM) to /
 * from the PLOGI or ADISC lists. Once the PLOGI and ADISC lists are populated,
 * we will first process the ADISC list.  32 entries are processed initially and
 * ADISC is initited for each one.  Completions / Events for each node are
 * funnelled thru the state machine.  As each node finishes ADISC processing, it
 * starts ADISC for any nodes waiting for ADISC processing. If no nodes are
 * waiting, and the ADISC list count is identically 0, then we are done. For
 * Link Up discovery, since all nodes on the PLOGI list are UNREG_LOGIN'ed, we
 * can issue a CLEAR_LA and reenable Link Events. Next we will process the PLOGI
 * list.  32 entries are processed initially and PLOGI is initited for each one.
 * Completions / Events for each node are funnelled thru the state machine.  As
 * each node finishes PLOGI processing, it starts PLOGI for any nodes waiting
 * for PLOGI processing. If no nodes are waiting, and the PLOGI list count is
 * identically 0, then we are done. We have now completed discovery / RSCN
 * handling. Upon completion, ALL nodes should be on either the mapped or
 * unmapped lists.
 */

/* Defines for Node List Entry Events that could happen */
#define NLP_EVT_RCV_PLOGI         0x0	/* Rcv'd an ELS PLOGI command */
#define NLP_EVT_RCV_PRLI          0x1	/* Rcv'd an ELS PRLI  command */
#define NLP_EVT_RCV_LOGO          0x2	/* Rcv'd an ELS LOGO  command */
#define NLP_EVT_RCV_ADISC         0x3	/* Rcv'd an ELS ADISC command */
#define NLP_EVT_RCV_PDISC         0x4	/* Rcv'd an ELS PDISC command */
#define NLP_EVT_RCV_PRLO          0x5	/* Rcv'd an ELS PRLO  command */
#define NLP_EVT_CMPL_PLOGI        0x6	/* Sent an ELS PLOGI command */
#define NLP_EVT_CMPL_PRLI         0x7	/* Sent an ELS PRLI  command */
#define NLP_EVT_CMPL_LOGO         0x8	/* Sent an ELS LOGO  command */
#define NLP_EVT_CMPL_ADISC        0x9	/* Sent an ELS ADISC command */
#define NLP_EVT_CMPL_REG_LOGIN    0xa	/* REG_LOGIN mbox cmd completed */
#define NLP_EVT_DEVICE_RM         0xb	/* Device not found in NS / ALPAmap */
#define NLP_EVT_DEVICE_RECOVERY   0xc	/* Device existence unknown */
#define NLP_EVT_MAX_EVENT         0xd

