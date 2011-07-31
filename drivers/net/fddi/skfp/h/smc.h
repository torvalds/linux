/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#ifndef	_SCMECM_
#define _SCMECM_

#if	defined(PCI) && !defined(OSDEF)
/*
 * In the case of the PCI bus the file osdef1st.h must be present
 */
#define	OSDEF
#endif

#ifdef	PCI
#ifndef	SUPERNET_3
#define	SUPERNET_3
#endif
#ifndef	TAG_MODE
#define	TAG_MODE
#endif
#endif

/*
 * include all other files in required order
 * the following files must have been included before:
 *	types.h
 *	fddi.h
 */
#ifdef	OSDEF
#include "h/osdef1st.h"
#endif	/* OSDEF */
#ifdef	OEM_CONCEPT
#include "oemdef.h"
#endif	/* OEM_CONCEPT */
#include "h/smt.h"
#include "h/cmtdef.h"
#include "h/fddimib.h"
#include "h/targethw.h"		/* all target hw dependencies */
#include "h/targetos.h"		/* all target os dependencies */
#ifdef	ESS
#include "h/sba.h"
#endif

/*
 * Event Queue
 *	queue.c
 * events are class/value pairs
 *	class	is addressee, e.g. RMT, PCM etc.
 *	value	is command, e.g. line state change, ring op change etc.
 */
struct event_queue {
	u_short	class ;			/* event class */
	u_short	event ;			/* event value */
} ;

/*
 * define event queue as circular buffer
 */
#ifdef	CONCENTRATOR
#define MAX_EVENT	128
#else	/* nCONCENTRATOR */
#define MAX_EVENT	64
#endif	/* nCONCENTRATOR */

struct s_queue {

	struct event_queue ev_queue[MAX_EVENT];
	struct event_queue *ev_put ;
	struct event_queue *ev_get ;
} ;

/*
 * ECM - Entity Coordination Management
 * ecm.c
 */
struct s_ecm {
	u_char path_test ;		/* ECM path test variable */
	u_char sb_flag ;		/* ECM stuck bypass */
	u_char DisconnectFlag ;		/* jd 05-Aug-1999 Bug #10419 
					 * ECM disconnected */
	u_char ecm_line_state ;		/* flag to dispatcher : line states */
	u_long trace_prop ;		/* ECM Trace_Prop flag >= 16 bits !! */
	/* NUMPHYS note:
	 * this variable must have enough bits to hold all entiies in
	 * the station. So NUMPHYS may not be greater than 31.
	 */
	char	ec_pad[2] ;
	struct smt_timer ecm_timer ;	/* timer */
} ;


/*
 * RMT - Ring Management
 * rmt.c
 */
struct s_rmt {
	u_char dup_addr_test ;		/* state of dupl. addr. test */
	u_char da_flag ;		/* flag : duplicate address det. */
	u_char loop_avail ;		/* flag : MAC available for loopback */
	u_char sm_ma_avail ;		/* flag : MAC available for SMT */
	u_char no_flag ;		/* flag : ring not operational */
	u_char bn_flag ;		/* flag : MAC reached beacon state */
	u_char jm_flag ;		/* flag : jamming in NON_OP_DUP */
	u_char rm_join ;		/* CFM flag RM_Join */
	u_char rm_loop ;		/* CFM flag RM_Loop */

	long fast_rm_join ;		/* bit mask of active ports */
	/*
	 * timer and flags
	 */
	struct smt_timer rmt_timer0 ;	/* timer 0 */
	struct smt_timer rmt_timer1 ;	/* timer 1 */
	struct smt_timer rmt_timer2 ;	/* timer 2 */
	u_char timer0_exp ;		/* flag : timer 0 expired */
	u_char timer1_exp ;		/* flag : timer 1 expired */
	u_char timer2_exp ;		/* flag : timer 2 expired */

	u_char rm_pad1[1] ;
} ;

/*
 * CFM - Configuration Management
 * cfm.c
 * used for SAS and DAS
 */
struct s_cfm {
	u_char cf_state;		/* CFM state machine current state */
	u_char cf_pad[3] ;
} ;

/*
 * CEM - Configuration Element Management
 * cem.c
 * used for Concentrator
 */
#ifdef	CONCENTRATOR
struct s_cem {
	int	ce_state ;	/* CEM state */
	int	ce_port ;	/* PA PB PM PM+1 .. */
	int	ce_type ;	/* TA TB TS TM */
} ;

/*
 * linked list of CCEs in current token path
 */
struct s_c_ring {
	struct s_c_ring	*c_next ;
	char		c_entity ;
} ;

struct mib_path_config {
	u_long	fddimibPATHConfigSMTIndex;
	u_long	fddimibPATHConfigPATHIndex;
	u_long	fddimibPATHConfigTokenOrder;
	u_long	fddimibPATHConfigResourceType;
#define SNMP_RES_TYPE_MAC	2	/* Resource is a MAC */
#define SNMP_RES_TYPE_PORT	4	/* Resource is a PORT */
	u_long	fddimibPATHConfigResourceIndex;
	u_long	fddimibPATHConfigCurrentPath;
#define SNMP_PATH_ISOLATED	1	/* Current path is isolated */
#define SNMP_PATH_LOCAL		2	/* Current path is local */
#define SNMP_PATH_SECONDARY	3	/* Current path is secondary */
#define SNMP_PATH_PRIMARY	4	/* Current path is primary */
#define SNMP_PATH_CONCATENATED	5	/* Current path is concatenated */
#define SNMP_PATH_THRU		6	/* Current path is thru */
};


#endif

/*
 * PCM connect states
 */
#define PCM_DISABLED	0
#define PCM_CONNECTING	1
#define PCM_STANDBY	2
#define PCM_ACTIVE	3

struct s_pcm {
	u_char	pcm_pad[3] ;
} ;

/*
 * PHY struct
 * one per physical port
 */
struct s_phy {
	/* Inter Module Globals */
	struct fddi_mib_p	*mib ;

	u_char np ;		/* index 0 .. NUMPHYS */
	u_char cf_join ;
	u_char cf_loop ;
	u_char wc_flag ;	/* withhold connection flag */
	u_char pc_mode ;	/* Holds the negotiated mode of the PCM */
	u_char pc_lem_fail ;	/* flag : LCT failed */
	u_char lc_test ;
	u_char scrub ;		/* CFM flag Scrub -> PCM */
	char phy_name ;
	u_char pmd_type[2] ;	/* SK connector/transceiver type codes */
#define PMD_SK_CONN	0	/* pmd_type[PMD_SK_CONN] = Connector */
#define PMD_SK_PMD	1	/* pmd_type[PMD_SK_PMD] = Xver */
	u_char pmd_scramble ;	/* scrambler on/off */

	/* inner Module Globals */
	u_char curr_ls ;	/* current line state */
	u_char ls_flag ;
	u_char rc_flag ;
	u_char tc_flag ;
	u_char td_flag ;
	u_char bitn ;
	u_char tr_flag ;	/* trace recvd while in active */
	u_char twisted ;	/* flag to indicate an A-A or B-B connection */
	u_char t_val[NUMBITS] ;	/* transmit bits for signaling */
	u_char r_val[NUMBITS] ;	/* receive bits for signaling */
	u_long t_next[NUMBITS] ;
	struct smt_timer pcm_timer0 ;
	struct smt_timer pcm_timer1 ;
	struct smt_timer pcm_timer2 ;
	u_char timer0_exp ;
	u_char timer1_exp ;
	u_char timer2_exp ;
	u_char pcm_pad1[1] ;
	int	cem_pst ;	/* CEM privae state; used for dual homing */
	struct lem_counter lem ;
#ifdef	AMDPLC
	struct s_plc	plc ;
#endif
} ;

/*
 * timer package
 * smttimer.c
 */
struct s_timer {
	struct smt_timer	*st_queue ;
	struct smt_timer	st_fast ;
} ;

/*
 * SRF types and data
 */
#define SMT_EVENT_BASE			1
#define SMT_EVENT_MAC_PATH_CHANGE	(SMT_EVENT_BASE+0)
#define SMT_EVENT_MAC_NEIGHBOR_CHANGE	(SMT_EVENT_BASE+1)
#define SMT_EVENT_PORT_PATH_CHANGE	(SMT_EVENT_BASE+2)
#define SMT_EVENT_PORT_CONNECTION	(SMT_EVENT_BASE+3)

#define SMT_IS_CONDITION(x)			((x)>=SMT_COND_BASE)

#define SMT_COND_BASE		(SMT_EVENT_PORT_CONNECTION+1)
#define SMT_COND_SMT_PEER_WRAP		(SMT_COND_BASE+0)
#define SMT_COND_SMT_HOLD		(SMT_COND_BASE+1)
#define SMT_COND_MAC_FRAME_ERROR	(SMT_COND_BASE+2)
#define SMT_COND_MAC_DUP_ADDR		(SMT_COND_BASE+3)
#define SMT_COND_MAC_NOT_COPIED		(SMT_COND_BASE+4)
#define SMT_COND_PORT_EB_ERROR		(SMT_COND_BASE+5)
#define SMT_COND_PORT_LER		(SMT_COND_BASE+6)

#define SR0_WAIT	0
#define SR1_HOLDOFF	1
#define SR2_DISABLED	2

struct s_srf {
	u_long	SRThreshold ;			/* threshold value */
	u_char	RT_Flag ;			/* report transmitted flag */
	u_char	sr_state ;			/* state-machine */
	u_char	any_report ;			/* any report required */
	u_long	TSR ;				/* timer */
	u_short	ring_status ;			/* IBM ring status */
} ;

/*
 * IBM token ring status
 */
#define RS_RES15	(1<<15)			/* reserved */
#define RS_HARDERROR	(1<<14)			/* ring down */
#define RS_SOFTERROR	(1<<13)			/* sent SRF */
#define RS_BEACON	(1<<12)			/* transmitted beacon */
#define RS_PATHTEST	(1<<11)			/* path test failed */
#define RS_SELFTEST	(1<<10)			/* selftest required */
#define RS_RES9		(1<< 9)			/* reserved */
#define RS_DISCONNECT	(1<< 8)			/* remote disconnect */
#define RS_RES7		(1<< 7)			/* reserved */
#define RS_DUPADDR	(1<< 6)			/* duplicate address */
#define RS_NORINGOP	(1<< 5)			/* no ring op */
#define RS_VERSION	(1<< 4)			/* SMT version mismatch */
#define RS_STUCKBYPASSS	(1<< 3)			/* stuck bypass */
#define RS_EVENT	(1<< 2)			/* FDDI event occurred */
#define RS_RINGOPCHANGE	(1<< 1)			/* ring op changed */
#define RS_RES0		(1<< 0)			/* reserved */

#define RS_SET(smc,bit) \
	ring_status_indication(smc,smc->srf.ring_status |= bit)
#define RS_CLEAR(smc,bit)	\
	ring_status_indication(smc,smc->srf.ring_status &= ~bit)

#define RS_CLEAR_EVENT	(0xffff & ~(RS_NORINGOP))

/* Define the AIX-event-Notification as null function if it isn't defined */
/* in the targetos.h file */
#ifndef AIX_EVENT
#define AIX_EVENT(smc,opt0,opt1,opt2,opt3)	/* nothing */
#endif

struct s_srf_evc {
	u_char	evc_code ;			/* event code type */
	u_char	evc_index ;			/* index for mult. instances */
	u_char	evc_rep_required ;		/* report required */
	u_short	evc_para ;			/* SMT Para Number */
	u_char	*evc_cond_state ;		/* condition state */
	u_char	*evc_multiple ;			/* multiple occurrence */
} ;

/*
 * Values used by frame based services
 * smt.c
 */
#define SMT_MAX_TEST		5
#define SMT_TID_NIF		0		/* pending NIF request */
#define SMT_TID_NIF_TEST	1		/* pending NIF test */
#define SMT_TID_ECF_UNA		2		/* pending ECF UNA test */
#define SMT_TID_ECF_DNA		3		/* pending ECF DNA test */
#define SMT_TID_ECF		4		/* pending ECF test */

struct smt_values {
	u_long		smt_tvu ;		/* timer valid una */
	u_long		smt_tvd ;		/* timer valid dna */
	u_long		smt_tid ;		/* transaction id */
	u_long		pend[SMT_MAX_TEST] ;	/* TID of requests */
	u_long		uniq_time ;		/* unique time stamp */
	u_short		uniq_ticks  ;		/* unique time stamp */
	u_short		please_reconnect ;	/* flag : reconnect */
	u_long		smt_last_lem ;
	u_long		smt_last_notify ;
	struct smt_timer	smt_timer ;	/* SMT NIF timer */
	u_long		last_tok_time[NUMMACS];	/* token cnt emulation */
} ;

/*
 * SMT/CMT configurable parameters
 */
#define SMT_DAS	0			/* dual attach */
#define SMT_SAS	1			/* single attach */
#define SMT_NAC	2			/* null attach concentrator */

struct smt_config {
	u_char	attach_s ;		/* CFM attach to secondary path */
	u_char	sas ;			/* SMT_DAS/SAS/NAC */
	u_char	build_ring_map ;	/* build ringmap if TRUE */
	u_char	numphys ;		/* number of active phys */
	u_char	sc_pad[1] ;

	u_long	pcm_tb_min ;		/* PCM : TB_Min timer value */
	u_long	pcm_tb_max ;		/* PCM : TB_Max timer value */
	u_long	pcm_c_min ;		/* PCM : C_Min timer value */
	u_long	pcm_t_out ;		/* PCM : T_Out timer value */
	u_long	pcm_tl_min ;		/* PCM : TL_min timer value */
	u_long	pcm_lc_short ;		/* PCM : LC_Short timer value */
	u_long	pcm_lc_medium ;		/* PCM : LC_Medium timer value */
	u_long	pcm_lc_long ;		/* PCM : LC_Long timer value */
	u_long	pcm_lc_extended ;	/* PCM : LC_Extended timer value */
	u_long	pcm_t_next_9 ;		/* PCM : T_Next[9] timer value */
	u_long	pcm_ns_max ;		/* PCM : NS_Max timer value */

	u_long	ecm_i_max ;		/* ECM : I_Max timer value */
	u_long	ecm_in_max ;		/* ECM : IN_Max timer value */
	u_long	ecm_td_min ;		/* ECM : TD_Min timer */
	u_long	ecm_test_done ;		/* ECM : path test done timer */
	u_long	ecm_check_poll ;	/* ECM : check bypass poller */

	u_long	rmt_t_non_op ;		/* RMT : T_Non_OP timer value */
	u_long	rmt_t_stuck ;		/* RMT : T_Stuck timer value */
	u_long	rmt_t_direct ;		/* RMT : T_Direct timer value */
	u_long	rmt_t_jam ;		/* RMT : T_Jam timer value */
	u_long	rmt_t_announce ;	/* RMT : T_Announce timer value */
	u_long	rmt_t_poll ;		/* RMT : claim/beacon poller */
	u_long  rmt_dup_mac_behavior ;  /* Flag for the beavior of SMT if
					 * a Duplicate MAC Address was detected.
					 * FALSE: SMT will leave finally the ring
					 * TRUE:  SMT will reinstert into the ring
					 */
	u_long	mac_d_max ;		/* MAC : D_Max timer value */

	u_long lct_short ;		/* LCT : error threshold */
	u_long lct_medium ;		/* LCT : error threshold */
	u_long lct_long ;		/* LCT : error threshold */
	u_long lct_extended ;		/* LCT : error threshold */
} ;

#ifdef	DEBUG
/*
 * Debugging struct sometimes used in smc
 */
struct	smt_debug {
	int	d_smtf ;
	int	d_smt ;
	int	d_ecm ;
	int	d_rmt ;
	int	d_cfm ;
	int	d_pcm ;
	int	d_plc ;
#ifdef	ESS
	int	d_ess ;
#endif
#ifdef	SBA
	int	d_sba ;
#endif
	struct	os_debug	d_os;	/* Include specific OS DEBUG struct */
} ;

#ifndef	DEBUG_BRD
/* all boards shall be debugged with one debug struct */
extern	struct	smt_debug	debug;	/* Declaration of debug struct */
#endif	/* DEBUG_BRD */

#endif	/* DEBUG */

/*
 * the SMT Context Struct SMC
 * this struct contains ALL global variables of SMT
 */
struct s_smc {
	struct s_smt_os	os ;		/* os specific */
	struct s_smt_hw	hw ;		/* hardware */

/*
 * NOTE: os and hw MUST BE the first two structs
 * anything beyond hw WILL BE SET TO ZERO in smt_set_defaults()
 */
	struct smt_config s ;		/* smt constants */
	struct smt_values sm ;		/* smt variables */
	struct s_ecm	e ;		/* ecm */
	struct s_rmt	r ;		/* rmt */
	struct s_cfm	cf ;		/* cfm/cem */
#ifdef	CONCENTRATOR
	struct s_cem	ce[NUMPHYS] ;	/* cem */
	struct s_c_ring	cr[NUMPHYS+NUMMACS] ;
#endif
	struct s_pcm	p ;		/* pcm */
	struct s_phy	y[NUMPHYS] ;	/* phy */
	struct s_queue	q ;		/* queue */
	struct s_timer	t ;		/* timer */
	struct s_srf srf ;		/* SRF */
	struct s_srf_evc evcs[6+NUMPHYS*4] ;
	struct fddi_mib	mib ;		/* __THE_MIB__ */
#ifdef	SBA
	struct s_sba	sba ;		/* SBA variables */
#endif
#ifdef	ESS
	struct s_ess	ess ;		/* Ess variables */
#endif
#if	defined(DEBUG) && defined(DEBUG_BRD)
	/* If you want all single board to be debugged separately */
	struct smt_debug	debug;	/* Declaration of debug struct */
#endif	/* DEBUG_BRD && DEBUG */
} ;

extern const struct fddi_addr fddi_broadcast;

extern void all_selection_criteria(struct s_smc *smc);
extern void card_stop(struct s_smc *smc);
extern void init_board(struct s_smc *smc, u_char *mac_addr);
extern int init_fplus(struct s_smc *smc);
extern void init_plc(struct s_smc *smc);
extern int init_smt(struct s_smc *smc, u_char * mac_addr);
extern void mac1_irq(struct s_smc *smc, u_short stu, u_short stl);
extern void mac2_irq(struct s_smc *smc, u_short code_s2u, u_short code_s2l);
extern void mac3_irq(struct s_smc *smc, u_short code_s3u, u_short code_s3l);
extern int pcm_status_twisted(struct s_smc *smc);
extern void plc1_irq(struct s_smc *smc);
extern void plc2_irq(struct s_smc *smc);
extern void read_address(struct s_smc *smc, u_char * mac_addr);
extern void timer_irq(struct s_smc *smc);

#endif	/* _SCMECM_ */

