/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	See the file "skfddi.c" for further information.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
	PCM
	Physical Connection Management
*/

/*
 * Hardware independent state machine implemantation
 * The following external SMT functions are referenced :
 *
 * 		queue_event()
 * 		smt_timer_start()
 * 		smt_timer_stop()
 *
 * 	The following external HW dependent functions are referenced :
 * 		sm_pm_control()
 *		sm_ph_linestate()
 *		sm_pm_ls_latch()
 *
 * 	The following HW dependent events are required :
 *		PC_QLS
 *		PC_ILS
 *		PC_HLS
 *		PC_MLS
 *		PC_NSE
 *		PC_LEM
 *
 */


#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"
#include "h/supern_2.h"
#define KERNEL
#include "h/smtstate.h"

#ifndef	lint
static const char ID_sccs[] = "@(#)pcmplc.c	2.55 99/08/05 (C) SK " ;
#endif

#ifdef	FDDI_MIB
extern int snmp_fddi_trap(
#ifdef	ANSIC
struct s_smc	* smc, int  type, int  index
#endif
);
#endif
#ifdef	CONCENTRATOR
extern int plc_is_installed(
#ifdef	ANSIC
struct s_smc *smc ,
int p
#endif
) ;
#endif
/*
 * FSM Macros
 */
#define AFLAG		(0x20)
#define GO_STATE(x)	(mib->fddiPORTPCMState = (x)|AFLAG)
#define ACTIONS_DONE()	(mib->fddiPORTPCMState &= ~AFLAG)
#define ACTIONS(x)	(x|AFLAG)

/*
 * PCM states
 */
#define PC0_OFF			0
#define PC1_BREAK		1
#define PC2_TRACE		2
#define PC3_CONNECT		3
#define PC4_NEXT		4
#define PC5_SIGNAL		5
#define PC6_JOIN		6
#define PC7_VERIFY		7
#define PC8_ACTIVE		8
#define PC9_MAINT		9

#ifdef	DEBUG
/*
 * symbolic state names
 */
static const char * const pcm_states[] =  {
	"PC0_OFF","PC1_BREAK","PC2_TRACE","PC3_CONNECT","PC4_NEXT",
	"PC5_SIGNAL","PC6_JOIN","PC7_VERIFY","PC8_ACTIVE","PC9_MAINT"
} ;

/*
 * symbolic event names
 */
static const char * const pcm_events[] = {
	"NONE","PC_START","PC_STOP","PC_LOOP","PC_JOIN","PC_SIGNAL",
	"PC_REJECT","PC_MAINT","PC_TRACE","PC_PDR",
	"PC_ENABLE","PC_DISABLE",
	"PC_QLS","PC_ILS","PC_MLS","PC_HLS","PC_LS_PDR","PC_LS_NONE",
	"PC_TIMEOUT_TB_MAX","PC_TIMEOUT_TB_MIN",
	"PC_TIMEOUT_C_MIN","PC_TIMEOUT_T_OUT",
	"PC_TIMEOUT_TL_MIN","PC_TIMEOUT_T_NEXT","PC_TIMEOUT_LCT",
	"PC_NSE","PC_LEM"
} ;
#endif

#ifdef	MOT_ELM
/*
 * PCL-S control register
 * this register in the PLC-S controls the scrambling parameters
 */
#define PLCS_CONTROL_C_U	0
#define PLCS_CONTROL_C_S	(PL_C_SDOFF_ENABLE | PL_C_SDON_ENABLE | \
				 PL_C_CIPHER_ENABLE)
#define	PLCS_FASSERT_U		0
#define	PLCS_FASSERT_S		0xFd76	/* 52.0 us */
#define	PLCS_FDEASSERT_U	0
#define	PLCS_FDEASSERT_S	0
#else	/* nMOT_ELM */
/*
 * PCL-S control register
 * this register in the PLC-S controls the scrambling parameters
 * can be patched for ANSI compliance if standard changes
 */
static const u_char plcs_control_c_u[17] = "PLC_CNTRL_C_U=\0\0" ;
static const u_char plcs_control_c_s[17] = "PLC_CNTRL_C_S=\01\02" ;

#define PLCS_CONTROL_C_U (plcs_control_c_u[14] | (plcs_control_c_u[15]<<8))
#define PLCS_CONTROL_C_S (plcs_control_c_s[14] | (plcs_control_c_s[15]<<8))
#endif	/* nMOT_ELM */

/*
 * external vars
 */
/* struct definition see 'cmtdef.h' (also used by CFM) */

#define PS_OFF		0
#define PS_BIT3		1
#define PS_BIT4		2
#define PS_BIT7		3
#define PS_LCT		4
#define PS_BIT8		5
#define PS_JOIN		6
#define PS_ACTIVE	7

#define LCT_LEM_MAX	255

/*
 * PLC timing parameter
 */

#define PLC_MS(m)	((int)((0x10000L-(m*100000L/2048))))
#define SLOW_TL_MIN	PLC_MS(6)
#define SLOW_C_MIN	PLC_MS(10)

static	const struct plt {
	int	timer ;			/* relative plc timer address */
	int	para ;			/* default timing parameters */
} pltm[] = {
	{ PL_C_MIN, SLOW_C_MIN },	/* min t. to remain Connect State */
	{ PL_TL_MIN, SLOW_TL_MIN },	/* min t. to transmit a Line State */
	{ PL_TB_MIN, TP_TB_MIN },	/* min break time */
	{ PL_T_OUT, TP_T_OUT },		/* Signaling timeout */
	{ PL_LC_LENGTH, TP_LC_LENGTH },	/* Link Confidence Test Time */
	{ PL_T_SCRUB, TP_T_SCRUB },	/* Scrub Time == MAC TVX time ! */
	{ PL_NS_MAX, TP_NS_MAX },	/* max t. that noise is tolerated */
	{ 0,0 }
} ;

/*
 * interrupt mask
 */
#ifdef	SUPERNET_3
/*
 * Do we need the EBUF error during signaling, too, to detect SUPERNET_3
 * PLL bug?
 */
static const int plc_imsk_na = PL_PCM_CODE | PL_TRACE_PROP | PL_PCM_BREAK |
			PL_PCM_ENABLED | PL_SELF_TEST | PL_EBUF_ERR;
#else	/* SUPERNET_3 */
/*
 * We do NOT need the elasticity buffer error during signaling.
 */
static int plc_imsk_na = PL_PCM_CODE | PL_TRACE_PROP | PL_PCM_BREAK |
			PL_PCM_ENABLED | PL_SELF_TEST ;
#endif	/* SUPERNET_3 */
static const int plc_imsk_act = PL_PCM_CODE | PL_TRACE_PROP | PL_PCM_BREAK |
			PL_PCM_ENABLED | PL_SELF_TEST | PL_EBUF_ERR;

/* internal functions */
static void pcm_fsm(struct s_smc *smc, struct s_phy *phy, int cmd);
static void pc_rcode_actions(struct s_smc *smc, int bit, struct s_phy *phy);
static void pc_tcode_actions(struct s_smc *smc, const int bit, struct s_phy *phy);
static void reset_lem_struct(struct s_phy *phy);
static void plc_init(struct s_smc *smc, int p);
static void sm_ph_lem_start(struct s_smc *smc, int np, int threshold);
static void sm_ph_lem_stop(struct s_smc *smc, int np);
static void sm_ph_linestate(struct s_smc *smc, int phy, int ls);
static void real_init_plc(struct s_smc *smc);

/*
 * SMT timer interface
 *      start PCM timer 0
 */
static void start_pcm_timer0(struct s_smc *smc, u_long value, int event,
			     struct s_phy *phy)
{
	phy->timer0_exp = FALSE ;       /* clear timer event flag */
	smt_timer_start(smc,&phy->pcm_timer0,value,
		EV_TOKEN(EVENT_PCM+phy->np,event)) ;
}
/*
 * SMT timer interface
 *      stop PCM timer 0
 */
static void stop_pcm_timer0(struct s_smc *smc, struct s_phy *phy)
{
	if (phy->pcm_timer0.tm_active)
		smt_timer_stop(smc,&phy->pcm_timer0) ;
}

/*
	init PCM state machine (called by driver)
	clear all PCM vars and flags
*/
void pcm_init(struct s_smc *smc)
{
	int		i ;
	int		np ;
	struct s_phy	*phy ;
	struct fddi_mib_p	*mib ;

	for (np = 0,phy = smc->y ; np < NUMPHYS ; np++,phy++) {
		/* Indicates the type of PHY being used */
		mib = phy->mib ;
		mib->fddiPORTPCMState = ACTIONS(PC0_OFF) ;
		phy->np = np ;
		switch (smc->s.sas) {
#ifdef	CONCENTRATOR
		case SMT_SAS :
			mib->fddiPORTMy_Type = (np == PS) ? TS : TM ;
			break ;
		case SMT_DAS :
			mib->fddiPORTMy_Type = (np == PA) ? TA :
					(np == PB) ? TB : TM ;
			break ;
		case SMT_NAC :
			mib->fddiPORTMy_Type = TM ;
			break;
#else
		case SMT_SAS :
			mib->fddiPORTMy_Type = (np == PS) ? TS : TNONE ;
			mib->fddiPORTHardwarePresent = (np == PS) ? TRUE :
					FALSE ;
#ifndef	SUPERNET_3
			smc->y[PA].mib->fddiPORTPCMState = PC0_OFF ;
#else
			smc->y[PB].mib->fddiPORTPCMState = PC0_OFF ;
#endif
			break ;
		case SMT_DAS :
			mib->fddiPORTMy_Type = (np == PB) ? TB : TA ;
			break ;
#endif
		}
		/*
		 * set PMD-type
		 */
		phy->pmd_scramble = 0 ;
		switch (phy->pmd_type[PMD_SK_PMD]) {
		case 'P' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_MULTI ;
			break ;
		case 'L' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_LCF ;
			break ;
		case 'D' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_TP ;
			break ;
		case 'S' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_TP ;
			phy->pmd_scramble = TRUE ;
			break ;
		case 'U' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_TP ;
			phy->pmd_scramble = TRUE ;
			break ;
		case '1' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_SINGLE1 ;
			break ;
		case '2' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_SINGLE2 ;
			break ;
		case '3' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_SINGLE2 ;
			break ;
		case '4' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_SINGLE1 ;
			break ;
		case 'H' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_UNKNOWN ;
			break ;
		case 'I' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_TP ;
			break ;
		case 'G' :
			mib->fddiPORTPMDClass = MIB_PMDCLASS_TP ;
			break ;
		default:
			mib->fddiPORTPMDClass = MIB_PMDCLASS_UNKNOWN ;
			break ;
		}
		/*
		 * A and B port can be on primary and secondary path
		 */
		switch (mib->fddiPORTMy_Type) {
		case TA :
			mib->fddiPORTAvailablePaths |= MIB_PATH_S ;
			mib->fddiPORTRequestedPaths[1] = MIB_P_PATH_LOCAL ;
			mib->fddiPORTRequestedPaths[2] =
				MIB_P_PATH_LOCAL |
				MIB_P_PATH_CON_ALTER |
				MIB_P_PATH_SEC_PREFER ;
			mib->fddiPORTRequestedPaths[3] =
				MIB_P_PATH_LOCAL |
				MIB_P_PATH_CON_ALTER |
				MIB_P_PATH_SEC_PREFER |
				MIB_P_PATH_THRU ;
			break ;
		case TB :
			mib->fddiPORTAvailablePaths |= MIB_PATH_S ;
			mib->fddiPORTRequestedPaths[1] = MIB_P_PATH_LOCAL ;
			mib->fddiPORTRequestedPaths[2] =
				MIB_P_PATH_LOCAL |
				MIB_P_PATH_PRIM_PREFER ;
			mib->fddiPORTRequestedPaths[3] =
				MIB_P_PATH_LOCAL |
				MIB_P_PATH_PRIM_PREFER |
				MIB_P_PATH_CON_PREFER |
				MIB_P_PATH_THRU ;
			break ;
		case TS :
			mib->fddiPORTAvailablePaths |= MIB_PATH_S ;
			mib->fddiPORTRequestedPaths[1] = MIB_P_PATH_LOCAL ;
			mib->fddiPORTRequestedPaths[2] =
				MIB_P_PATH_LOCAL |
				MIB_P_PATH_CON_ALTER |
				MIB_P_PATH_PRIM_PREFER ;
			mib->fddiPORTRequestedPaths[3] =
				MIB_P_PATH_LOCAL |
				MIB_P_PATH_CON_ALTER |
				MIB_P_PATH_PRIM_PREFER ;
			break ;
		case TM :
			mib->fddiPORTRequestedPaths[1] = MIB_P_PATH_LOCAL ;
			mib->fddiPORTRequestedPaths[2] =
				MIB_P_PATH_LOCAL |
				MIB_P_PATH_SEC_ALTER |
				MIB_P_PATH_PRIM_ALTER ;
			mib->fddiPORTRequestedPaths[3] = 0 ;
			break ;
		}

		phy->pc_lem_fail = FALSE ;
		mib->fddiPORTPCMStateX = mib->fddiPORTPCMState ;
		mib->fddiPORTLCTFail_Ct = 0 ;
		mib->fddiPORTBS_Flag = 0 ;
		mib->fddiPORTCurrentPath = MIB_PATH_ISOLATED ;
		mib->fddiPORTNeighborType = TNONE ;
		phy->ls_flag = 0 ;
		phy->rc_flag = 0 ;
		phy->tc_flag = 0 ;
		phy->td_flag = 0 ;
		if (np >= PM)
			phy->phy_name = '0' + np - PM ;
		else
			phy->phy_name = 'A' + np ;
		phy->wc_flag = FALSE ;		/* set by SMT */
		memset((char *)&phy->lem,0,sizeof(struct lem_counter)) ;
		reset_lem_struct(phy) ;
		memset((char *)&phy->plc,0,sizeof(struct s_plc)) ;
		phy->plc.p_state = PS_OFF ;
		for (i = 0 ; i < NUMBITS ; i++) {
			phy->t_next[i] = 0 ;
		}
	}
	real_init_plc(smc) ;
}

void init_plc(struct s_smc *smc)
{
	SK_UNUSED(smc) ;

	/*
	 * dummy
	 * this is an obsolete public entry point that has to remain
	 * for compat. It is used by various drivers.
	 * the work is now done in real_init_plc()
	 * which is called from pcm_init() ;
	 */
}

static void real_init_plc(struct s_smc *smc)
{
	int	p ;

	for (p = 0 ; p < NUMPHYS ; p++)
		plc_init(smc,p) ;
}

static void plc_init(struct s_smc *smc, int p)
{
	int	i ;
#ifndef	MOT_ELM
	int	rev ;	/* Revision of PLC-x */
#endif	/* MOT_ELM */

	/* transit PCM state machine to MAINT state */
	outpw(PLC(p,PL_CNTRL_B),0) ;
	outpw(PLC(p,PL_CNTRL_B),PL_PCM_STOP) ;
	outpw(PLC(p,PL_CNTRL_A),0) ;

	/*
	 * if PLC-S then set control register C
	 */
#ifndef	MOT_ELM
	rev = inpw(PLC(p,PL_STATUS_A)) & PLC_REV_MASK ;
	if (rev != PLC_REVISION_A)
#endif	/* MOT_ELM */
	{
		if (smc->y[p].pmd_scramble) {
			outpw(PLC(p,PL_CNTRL_C),PLCS_CONTROL_C_S) ;
#ifdef	MOT_ELM
			outpw(PLC(p,PL_T_FOT_ASS),PLCS_FASSERT_S) ;
			outpw(PLC(p,PL_T_FOT_DEASS),PLCS_FDEASSERT_S) ;
#endif	/* MOT_ELM */
		}
		else {
			outpw(PLC(p,PL_CNTRL_C),PLCS_CONTROL_C_U) ;
#ifdef	MOT_ELM
			outpw(PLC(p,PL_T_FOT_ASS),PLCS_FASSERT_U) ;
			outpw(PLC(p,PL_T_FOT_DEASS),PLCS_FDEASSERT_U) ;
#endif	/* MOT_ELM */
		}
	}

	/*
	 * set timer register
	 */
	for ( i = 0 ; pltm[i].timer; i++)	/* set timer parameter reg */
		outpw(PLC(p,pltm[i].timer),pltm[i].para) ;

	(void)inpw(PLC(p,PL_INTR_EVENT)) ;	/* clear interrupt event reg */
	plc_clear_irq(smc,p) ;
	outpw(PLC(p,PL_INTR_MASK),plc_imsk_na); /* enable non active irq's */

	/*
	 * if PCM is configured for class s, it will NOT go to the
	 * REMOVE state if offline (page 3-36;)
	 * in the concentrator, all inactive PHYS always must be in
	 * the remove state
	 * there's no real need to use this feature at all ..
	 */
#ifndef	CONCENTRATOR
	if ((smc->s.sas == SMT_SAS) && (p == PS)) {
		outpw(PLC(p,PL_CNTRL_B),PL_CLASS_S) ;
	}
#endif
}

/*
 * control PCM state machine
 */
static void plc_go_state(struct s_smc *smc, int p, int state)
{
	HW_PTR port ;
	int val ;

	SK_UNUSED(smc) ;

	port = (HW_PTR) (PLC(p,PL_CNTRL_B)) ;
	val = inpw(port) & ~(PL_PCM_CNTRL | PL_MAINT) ;
	outpw(port,val) ;
	outpw(port,val | state) ;
}

/*
 * read current line state (called by ECM & PCM)
 */
int sm_pm_get_ls(struct s_smc *smc, int phy)
{
	int	state ;

#ifdef	CONCENTRATOR
	if (!plc_is_installed(smc,phy))
		return PC_QLS;
#endif

	state = inpw(PLC(phy,PL_STATUS_A)) & PL_LINE_ST ;
	switch(state) {
	case PL_L_QLS:
		state = PC_QLS ;
		break ;
	case PL_L_MLS:
		state = PC_MLS ;
		break ;
	case PL_L_HLS:
		state = PC_HLS ;
		break ;
	case PL_L_ILS4:
	case PL_L_ILS16:
		state = PC_ILS ;
		break ;
	case PL_L_ALS:
		state = PC_LS_PDR ;
		break ;
	default :
		state = PC_LS_NONE ;
	}
	return state;
}

static int plc_send_bits(struct s_smc *smc, struct s_phy *phy, int len)
{
	int np = phy->np ;		/* PHY index */
	int	n ;
	int	i ;

	SK_UNUSED(smc) ;

	/* create bit vector */
	for (i = len-1,n = 0 ; i >= 0 ; i--) {
		n = (n<<1) | phy->t_val[phy->bitn+i] ;
	}
	if (inpw(PLC(np,PL_STATUS_B)) & PL_PCM_SIGNAL) {
#if	0
		printf("PL_PCM_SIGNAL is set\n") ;
#endif
		return 1;
	}
	/* write bit[n] & length = 1 to regs */
	outpw(PLC(np,PL_VECTOR_LEN),len-1) ;	/* len=nr-1 */
	outpw(PLC(np,PL_XMIT_VECTOR),n) ;
#ifdef	DEBUG
#if 1
#ifdef	DEBUG_BRD
	if (smc->debug.d_plc & 0x80)
#else
	if (debug.d_plc & 0x80)
#endif
		printf("SIGNALING bit %d .. %d\n",phy->bitn,phy->bitn+len-1) ;
#endif
#endif
	return 0;
}

/*
 * config plc muxes
 */
void plc_config_mux(struct s_smc *smc, int mux)
{
	if (smc->s.sas != SMT_DAS)
		return ;
	if (mux == MUX_WRAPB) {
		SETMASK(PLC(PA,PL_CNTRL_B),PL_CONFIG_CNTRL,PL_CONFIG_CNTRL) ;
		SETMASK(PLC(PA,PL_CNTRL_A),PL_SC_REM_LOOP,PL_SC_REM_LOOP) ;
	}
	else {
		CLEAR(PLC(PA,PL_CNTRL_B),PL_CONFIG_CNTRL) ;
		CLEAR(PLC(PA,PL_CNTRL_A),PL_SC_REM_LOOP) ;
	}
	CLEAR(PLC(PB,PL_CNTRL_B),PL_CONFIG_CNTRL) ;
	CLEAR(PLC(PB,PL_CNTRL_A),PL_SC_REM_LOOP) ;
}

/*
	PCM state machine
	called by dispatcher  & fddi_init() (driver)
	do
		display state change
		process event
	until SM is stable
*/
void pcm(struct s_smc *smc, const int np, int event)
{
	int	state ;
	int	oldstate ;
	struct s_phy	*phy ;
	struct fddi_mib_p	*mib ;

#ifndef	CONCENTRATOR
	/*
	 * ignore 2nd PHY if SAS
	 */
	if ((np != PS) && (smc->s.sas == SMT_SAS))
		return ;
#endif
	phy = &smc->y[np] ;
	mib = phy->mib ;
	oldstate = mib->fddiPORTPCMState ;
	do {
		DB_PCM("PCM %c: state %s",
			phy->phy_name,
			(mib->fddiPORTPCMState & AFLAG) ? "ACTIONS " : "") ;
		DB_PCM("%s, event %s\n",
			pcm_states[mib->fddiPORTPCMState & ~AFLAG],
			pcm_events[event]) ;
		state = mib->fddiPORTPCMState ;
		pcm_fsm(smc,phy,event) ;
		event = 0 ;
	} while (state != mib->fddiPORTPCMState) ;
	/*
	 * because the PLC does the bit signaling for us,
	 * we're always in SIGNAL state
	 * the MIB want's to see CONNECT
	 * we therefore fake an entry in the MIB
	 */
	if (state == PC5_SIGNAL)
		mib->fddiPORTPCMStateX = PC3_CONNECT ;
	else
		mib->fddiPORTPCMStateX = state ;

#ifndef	SLIM_SMT
	/*
	 * path change
	 */
	if (	mib->fddiPORTPCMState != oldstate &&
		((oldstate == PC8_ACTIVE) || (mib->fddiPORTPCMState == PC8_ACTIVE))) {
		smt_srf_event(smc,SMT_EVENT_PORT_PATH_CHANGE,
			(int) (INDEX_PORT+ phy->np),0) ;
	}
#endif

#ifdef FDDI_MIB
	/* check whether a snmp-trap has to be sent */

	if ( mib->fddiPORTPCMState != oldstate ) {
		/* a real state change took place */
		DB_SNMP ("PCM from %d to %d\n", oldstate, mib->fddiPORTPCMState);
		if ( mib->fddiPORTPCMState == PC0_OFF ) {
			/* send first trap */
			snmp_fddi_trap (smc, 1, (int) mib->fddiPORTIndex );
		} else if ( oldstate == PC0_OFF ) {
			/* send second trap */
			snmp_fddi_trap (smc, 2, (int) mib->fddiPORTIndex );
		} else if ( mib->fddiPORTPCMState != PC2_TRACE &&
			oldstate == PC8_ACTIVE ) {
			/* send third trap */
			snmp_fddi_trap (smc, 3, (int) mib->fddiPORTIndex );
		} else if ( mib->fddiPORTPCMState == PC8_ACTIVE ) {
			/* send fourth trap */
			snmp_fddi_trap (smc, 4, (int) mib->fddiPORTIndex );
		}
	}
#endif

	pcm_state_change(smc,np,state) ;
}

/*
 * PCM state machine
 */
static void pcm_fsm(struct s_smc *smc, struct s_phy *phy, int cmd)
{
	int	i ;
	int	np = phy->np ;		/* PHY index */
	struct s_plc	*plc ;
	struct fddi_mib_p	*mib ;
#ifndef	MOT_ELM
	u_short	plc_rev ;		/* Revision of the plc */
#endif	/* nMOT_ELM */

	plc = &phy->plc ;
	mib = phy->mib ;

	/*
	 * general transitions independent of state
	 */
	switch (cmd) {
	case PC_STOP :
		/*PC00-PC80*/
		if (mib->fddiPORTPCMState != PC9_MAINT) {
			GO_STATE(PC0_OFF) ;
			AIX_EVENT(smc, (u_long) FDDI_RING_STATUS, (u_long)
				FDDI_PORT_EVENT, (u_long) FDDI_PORT_STOP,
				smt_get_port_event_word(smc));
		}
		return ;
	case PC_START :
		/*PC01-PC81*/
		if (mib->fddiPORTPCMState != PC9_MAINT)
			GO_STATE(PC1_BREAK) ;
		return ;
	case PC_DISABLE :
		/* PC09-PC99 */
		GO_STATE(PC9_MAINT) ;
		AIX_EVENT(smc, (u_long) FDDI_RING_STATUS, (u_long)
			FDDI_PORT_EVENT, (u_long) FDDI_PORT_DISABLED,
			smt_get_port_event_word(smc));
		return ;
	case PC_TIMEOUT_LCT :
		/* if long or extended LCT */
		stop_pcm_timer0(smc,phy) ;
		CLEAR(PLC(np,PL_CNTRL_B),PL_LONG) ;
		/* end of LCT is indicate by PCM_CODE (initiate PCM event) */
		return ;
	}

	switch(mib->fddiPORTPCMState) {
	case ACTIONS(PC0_OFF) :
		stop_pcm_timer0(smc,phy) ;
		outpw(PLC(np,PL_CNTRL_A),0) ;
		CLEAR(PLC(np,PL_CNTRL_B),PL_PC_JOIN) ;
		CLEAR(PLC(np,PL_CNTRL_B),PL_LONG) ;
		sm_ph_lem_stop(smc,np) ;		/* disable LEM */
		phy->cf_loop = FALSE ;
		phy->cf_join = FALSE ;
		queue_event(smc,EVENT_CFM,CF_JOIN+np) ;
		plc_go_state(smc,np,PL_PCM_STOP) ;
		mib->fddiPORTConnectState = PCM_DISABLED ;
		ACTIONS_DONE() ;
		break ;
	case PC0_OFF:
		/*PC09*/
		if (cmd == PC_MAINT) {
			GO_STATE(PC9_MAINT) ;
			break ;
		}
		break ;
	case ACTIONS(PC1_BREAK) :
		/* Stop the LCT timer if we came from Signal state */
		stop_pcm_timer0(smc,phy) ;
		ACTIONS_DONE() ;
		plc_go_state(smc,np,0) ;
		CLEAR(PLC(np,PL_CNTRL_B),PL_PC_JOIN) ;
		CLEAR(PLC(np,PL_CNTRL_B),PL_LONG) ;
		sm_ph_lem_stop(smc,np) ;		/* disable LEM */
		/*
		 * if vector is already loaded, go to OFF to clear PCM_SIGNAL
		 */
#if	0
		if (inpw(PLC(np,PL_STATUS_B)) & PL_PCM_SIGNAL) {
			plc_go_state(smc,np,PL_PCM_STOP) ;
			/* TB_MIN ? */
		}
#endif
		/*
		 * Go to OFF state in any case.
		 */
		plc_go_state(smc,np,PL_PCM_STOP) ;

		if (mib->fddiPORTPC_Withhold == PC_WH_NONE)
			mib->fddiPORTConnectState = PCM_CONNECTING ;
		phy->cf_loop = FALSE ;
		phy->cf_join = FALSE ;
		queue_event(smc,EVENT_CFM,CF_JOIN+np) ;
		phy->ls_flag = FALSE ;
		phy->pc_mode = PM_NONE ;	/* needed by CFM */
		phy->bitn = 0 ;			/* bit signaling start bit */
		for (i = 0 ; i < 3 ; i++)
			pc_tcode_actions(smc,i,phy) ;

		/* Set the non-active interrupt mask register */
		outpw(PLC(np,PL_INTR_MASK),plc_imsk_na) ;

		/*
		 * If the LCT was stopped. There might be a
		 * PCM_CODE interrupt event present.
		 * This must be cleared.
		 */
		(void)inpw(PLC(np,PL_INTR_EVENT)) ;
#ifndef	MOT_ELM
		/* Get the plc revision for revision dependent code */
		plc_rev = inpw(PLC(np,PL_STATUS_A)) & PLC_REV_MASK ;

		if (plc_rev != PLC_REV_SN3)
#endif	/* MOT_ELM */
		{
			/*
			 * No supernet III PLC, so set Xmit verctor and
			 * length BEFORE starting the state machine.
			 */
			if (plc_send_bits(smc,phy,3)) {
				return ;
			}
		}

		/*
		 * Now give the Start command.
		 * - The start command shall be done before setting the bits
		 *   to be signaled. (In PLC-S description and PLCS in SN3.
		 * - The start command shall be issued AFTER setting the
		 *   XMIT vector and the XMIT length register.
		 *
		 * We do it exactly according this specs for the old PLC and
		 * the new PLCS inside the SN3.
		 * For the usual PLCS we try it the way it is done for the
		 * old PLC and set the XMIT registers again, if the PLC is
		 * not in SIGNAL state. This is done according to an PLCS
		 * errata workaround.
		 */

		plc_go_state(smc,np,PL_PCM_START) ;

		/*
		 * workaround for PLC-S eng. sample errata
		 */
#ifdef	MOT_ELM
		if (!(inpw(PLC(np,PL_STATUS_B)) & PL_PCM_SIGNAL))
#else	/* nMOT_ELM */
		if (((inpw(PLC(np,PL_STATUS_A)) & PLC_REV_MASK) !=
			PLC_REVISION_A) &&
			!(inpw(PLC(np,PL_STATUS_B)) & PL_PCM_SIGNAL))
#endif	/* nMOT_ELM */
		{
			/*
			 * Set register again (PLCS errata) or the first time
			 * (new SN3 PLCS).
			 */
			(void) plc_send_bits(smc,phy,3) ;
		}
		/*
		 * end of workaround
		 */

		GO_STATE(PC5_SIGNAL) ;
		plc->p_state = PS_BIT3 ;
		plc->p_bits = 3 ;
		plc->p_start = 0 ;

		break ;
	case PC1_BREAK :
		break ;
	case ACTIONS(PC2_TRACE) :
		plc_go_state(smc,np,PL_PCM_TRACE) ;
		ACTIONS_DONE() ;
		break ;
	case PC2_TRACE :
		break ;

	case PC3_CONNECT :	/* these states are done by hardware */
	case PC4_NEXT :
		break ;

	case ACTIONS(PC5_SIGNAL) :
		ACTIONS_DONE() ;
	case PC5_SIGNAL :
		if ((cmd != PC_SIGNAL) && (cmd != PC_TIMEOUT_LCT))
			break ;
		switch (plc->p_state) {
		case PS_BIT3 :
			for (i = 0 ; i <= 2 ; i++)
				pc_rcode_actions(smc,i,phy) ;
			pc_tcode_actions(smc,3,phy) ;
			plc->p_state = PS_BIT4 ;
			plc->p_bits = 1 ;
			plc->p_start = 3 ;
			phy->bitn = 3 ;
			if (plc_send_bits(smc,phy,1)) {
				return ;
			}
			break ;
		case PS_BIT4 :
			pc_rcode_actions(smc,3,phy) ;
			for (i = 4 ; i <= 6 ; i++)
				pc_tcode_actions(smc,i,phy) ;
			plc->p_state = PS_BIT7 ;
			plc->p_bits = 3 ;
			plc->p_start = 4 ;
			phy->bitn = 4 ;
			if (plc_send_bits(smc,phy,3)) {
				return ;
			}
			break ;
		case PS_BIT7 :
			for (i = 3 ; i <= 6 ; i++)
				pc_rcode_actions(smc,i,phy) ;
			plc->p_state = PS_LCT ;
			plc->p_bits = 0 ;
			plc->p_start = 7 ;
			phy->bitn = 7 ;
		sm_ph_lem_start(smc,np,(int)smc->s.lct_short) ; /* enable LEM */
			/* start LCT */
			i = inpw(PLC(np,PL_CNTRL_B)) & ~PL_PC_LOOP ;
			outpw(PLC(np,PL_CNTRL_B),i) ;	/* must be cleared */
			outpw(PLC(np,PL_CNTRL_B),i | PL_RLBP) ;
			break ;
		case PS_LCT :
			/* check for local LCT failure */
			pc_tcode_actions(smc,7,phy) ;
			/*
			 * set tval[7]
			 */
			plc->p_state = PS_BIT8 ;
			plc->p_bits = 1 ;
			plc->p_start = 7 ;
			phy->bitn = 7 ;
			if (plc_send_bits(smc,phy,1)) {
				return ;
			}
			break ;
		case PS_BIT8 :
			/* check for remote LCT failure */
			pc_rcode_actions(smc,7,phy) ;
			if (phy->t_val[7] || phy->r_val[7]) {
				plc_go_state(smc,np,PL_PCM_STOP) ;
				GO_STATE(PC1_BREAK) ;
				break ;
			}
			for (i = 8 ; i <= 9 ; i++)
				pc_tcode_actions(smc,i,phy) ;
			plc->p_state = PS_JOIN ;
			plc->p_bits = 2 ;
			plc->p_start = 8 ;
			phy->bitn = 8 ;
			if (plc_send_bits(smc,phy,2)) {
				return ;
			}
			break ;
		case PS_JOIN :
			for (i = 8 ; i <= 9 ; i++)
				pc_rcode_actions(smc,i,phy) ;
			plc->p_state = PS_ACTIVE ;
			GO_STATE(PC6_JOIN) ;
			break ;
		}
		break ;

	case ACTIONS(PC6_JOIN) :
		/*
		 * prevent mux error when going from WRAP_A to WRAP_B
		 */
		if (smc->s.sas == SMT_DAS && np == PB &&
			(smc->y[PA].pc_mode == PM_TREE ||
			 smc->y[PB].pc_mode == PM_TREE)) {
			SETMASK(PLC(np,PL_CNTRL_A),
				PL_SC_REM_LOOP,PL_SC_REM_LOOP) ;
			SETMASK(PLC(np,PL_CNTRL_B),
				PL_CONFIG_CNTRL,PL_CONFIG_CNTRL) ;
		}
		SETMASK(PLC(np,PL_CNTRL_B),PL_PC_JOIN,PL_PC_JOIN) ;
		SETMASK(PLC(np,PL_CNTRL_B),PL_PC_JOIN,PL_PC_JOIN) ;
		ACTIONS_DONE() ;
		cmd = 0 ;
		/* fall thru */
	case PC6_JOIN :
		switch (plc->p_state) {
		case PS_ACTIVE:
			/*PC88b*/
			if (!phy->cf_join) {
				phy->cf_join = TRUE ;
				queue_event(smc,EVENT_CFM,CF_JOIN+np) ;
			}
			if (cmd == PC_JOIN)
				GO_STATE(PC8_ACTIVE) ;
			/*PC82*/
			if (cmd == PC_TRACE) {
				GO_STATE(PC2_TRACE) ;
				break ;
			}
			break ;
		}
		break ;

	case PC7_VERIFY :
		break ;

	case ACTIONS(PC8_ACTIVE) :
		/*
		 * start LEM for SMT
		 */
		sm_ph_lem_start(smc,(int)phy->np,LCT_LEM_MAX) ;

		phy->tr_flag = FALSE ;
		mib->fddiPORTConnectState = PCM_ACTIVE ;

		/* Set the active interrupt mask register */
		outpw(PLC(np,PL_INTR_MASK),plc_imsk_act) ;

		ACTIONS_DONE() ;
		break ;
	case PC8_ACTIVE :
		/*PC81 is done by PL_TNE_EXPIRED irq */
		/*PC82*/
		if (cmd == PC_TRACE) {
			GO_STATE(PC2_TRACE) ;
			break ;
		}
		/*PC88c: is done by TRACE_PROP irq */

		break ;
	case ACTIONS(PC9_MAINT) :
		stop_pcm_timer0(smc,phy) ;
		CLEAR(PLC(np,PL_CNTRL_B),PL_PC_JOIN) ;
		CLEAR(PLC(np,PL_CNTRL_B),PL_LONG) ;
		CLEAR(PLC(np,PL_INTR_MASK),PL_LE_CTR) ;	/* disable LEM int. */
		sm_ph_lem_stop(smc,np) ;		/* disable LEM */
		phy->cf_loop = FALSE ;
		phy->cf_join = FALSE ;
		queue_event(smc,EVENT_CFM,CF_JOIN+np) ;
		plc_go_state(smc,np,PL_PCM_STOP) ;
		mib->fddiPORTConnectState = PCM_DISABLED ;
		SETMASK(PLC(np,PL_CNTRL_B),PL_MAINT,PL_MAINT) ;
		sm_ph_linestate(smc,np,(int) MIB2LS(mib->fddiPORTMaint_LS)) ;
		outpw(PLC(np,PL_CNTRL_A),PL_SC_BYPASS) ;
		ACTIONS_DONE() ;
		break ;
	case PC9_MAINT :
		DB_PCMN(1,"PCM %c : MAINT\n",phy->phy_name,0) ;
		/*PC90*/
		if (cmd == PC_ENABLE) {
			GO_STATE(PC0_OFF) ;
			break ;
		}
		break ;

	default:
		SMT_PANIC(smc,SMT_E0118, SMT_E0118_MSG) ;
		break ;
	}
}

/*
 * force line state on a PHY output	(only in MAINT state)
 */
static void sm_ph_linestate(struct s_smc *smc, int phy, int ls)
{
	int	cntrl ;

	SK_UNUSED(smc) ;

	cntrl = (inpw(PLC(phy,PL_CNTRL_B)) & ~PL_MAINT_LS) |
						PL_PCM_STOP | PL_MAINT ;
	switch(ls) {
	case PC_QLS: 		/* Force Quiet */
		cntrl |= PL_M_QUI0 ;
		break ;
	case PC_MLS: 		/* Force Master */
		cntrl |= PL_M_MASTR ;
		break ;
	case PC_HLS: 		/* Force Halt */
		cntrl |= PL_M_HALT ;
		break ;
	default :
	case PC_ILS: 		/* Force Idle */
		cntrl |= PL_M_IDLE ;
		break ;
	case PC_LS_PDR: 	/* Enable repeat filter */
		cntrl |= PL_M_TPDR ;
		break ;
	}
	outpw(PLC(phy,PL_CNTRL_B),cntrl) ;
}

static void reset_lem_struct(struct s_phy *phy)
{
	struct lem_counter *lem = &phy->lem ;

	phy->mib->fddiPORTLer_Estimate = 15 ;
	lem->lem_float_ber = 15 * 100 ;
}

/*
 * link error monitor
 */
static void lem_evaluate(struct s_smc *smc, struct s_phy *phy)
{
	int ber ;
	u_long errors ;
	struct lem_counter *lem = &phy->lem ;
	struct fddi_mib_p	*mib ;
	int			cond ;

	mib = phy->mib ;

	if (!lem->lem_on)
		return ;

	errors = inpw(PLC(((int) phy->np),PL_LINK_ERR_CTR)) ;
	lem->lem_errors += errors ;
	mib->fddiPORTLem_Ct += errors ;

	errors = lem->lem_errors ;
	/*
	 * calculation is called on a intervall of 8 seconds
	 *	-> this means, that one error in 8 sec. is one of 8*125*10E6
	 *	the same as BER = 10E-9
	 * Please note:
	 *	-> 9 errors in 8 seconds mean:
	 *	   BER = 9 * 10E-9  and this is
	 *	    < 10E-8, so the limit of 10E-8 is not reached!
	 */

		if (!errors)		ber = 15 ;
	else	if (errors <= 9)	ber = 9 ;
	else	if (errors <= 99)	ber = 8 ;
	else	if (errors <= 999)	ber = 7 ;
	else	if (errors <= 9999)	ber = 6 ;
	else	if (errors <= 99999)	ber = 5 ;
	else	if (errors <= 999999)	ber = 4 ;
	else	if (errors <= 9999999)	ber = 3 ;
	else	if (errors <= 99999999)	ber = 2 ;
	else	if (errors <= 999999999) ber = 1 ;
	else				ber = 0 ;

	/*
	 * weighted average
	 */
	ber *= 100 ;
	lem->lem_float_ber = lem->lem_float_ber * 7 + ber * 3 ;
	lem->lem_float_ber /= 10 ;
	mib->fddiPORTLer_Estimate = lem->lem_float_ber / 100 ;
	if (mib->fddiPORTLer_Estimate < 4) {
		mib->fddiPORTLer_Estimate = 4 ;
	}

	if (lem->lem_errors) {
		DB_PCMN(1,"LEM %c :\n",phy->np == PB? 'B' : 'A',0) ;
		DB_PCMN(1,"errors      : %ld\n",lem->lem_errors,0) ;
		DB_PCMN(1,"sum_errors  : %ld\n",mib->fddiPORTLem_Ct,0) ;
		DB_PCMN(1,"current BER : 10E-%d\n",ber/100,0) ;
		DB_PCMN(1,"float BER   : 10E-(%d/100)\n",lem->lem_float_ber,0) ;
		DB_PCMN(1,"avg. BER    : 10E-%d\n",
			mib->fddiPORTLer_Estimate,0) ;
	}

	lem->lem_errors = 0L ;

#ifndef	SLIM_SMT
	cond = (mib->fddiPORTLer_Estimate <= mib->fddiPORTLer_Alarm) ?
		TRUE : FALSE ;
#ifdef	SMT_EXT_CUTOFF
	smt_ler_alarm_check(smc,phy,cond) ;
#endif	/* nSMT_EXT_CUTOFF */
	if (cond != mib->fddiPORTLerFlag) {
		smt_srf_event(smc,SMT_COND_PORT_LER,
			(int) (INDEX_PORT+ phy->np) ,cond) ;
	}
#endif

	if (	mib->fddiPORTLer_Estimate <= mib->fddiPORTLer_Cutoff) {
		phy->pc_lem_fail = TRUE ;		/* flag */
		mib->fddiPORTLem_Reject_Ct++ ;
		/*
		 * "forgive 10e-2" if we cutoff so we can come
		 * up again ..
		 */
		lem->lem_float_ber += 2*100 ;

		/*PC81b*/
#ifdef	CONCENTRATOR
		DB_PCMN(1,"PCM: LER cutoff on port %d cutoff %d\n",
			phy->np, mib->fddiPORTLer_Cutoff) ;
#endif
#ifdef	SMT_EXT_CUTOFF
		smt_port_off_event(smc,phy->np);
#else	/* nSMT_EXT_CUTOFF */
		queue_event(smc,(int)(EVENT_PCM+phy->np),PC_START) ;
#endif	/* nSMT_EXT_CUTOFF */
	}
}

/*
 * called by SMT to calculate LEM bit error rate
 */
void sm_lem_evaluate(struct s_smc *smc)
{
	int np ;

	for (np = 0 ; np < NUMPHYS ; np++)
		lem_evaluate(smc,&smc->y[np]) ;
}

static void lem_check_lct(struct s_smc *smc, struct s_phy *phy)
{
	struct lem_counter	*lem = &phy->lem ;
	struct fddi_mib_p	*mib ;
	int errors ;

	mib = phy->mib ;

	phy->pc_lem_fail = FALSE ;		/* flag */
	errors = inpw(PLC(((int)phy->np),PL_LINK_ERR_CTR)) ;
	lem->lem_errors += errors ;
	mib->fddiPORTLem_Ct += errors ;
	if (lem->lem_errors) {
		switch(phy->lc_test) {
		case LC_SHORT:
			if (lem->lem_errors >= smc->s.lct_short)
				phy->pc_lem_fail = TRUE ;
			break ;
		case LC_MEDIUM:
			if (lem->lem_errors >= smc->s.lct_medium)
				phy->pc_lem_fail = TRUE ;
			break ;
		case LC_LONG:
			if (lem->lem_errors >= smc->s.lct_long)
				phy->pc_lem_fail = TRUE ;
			break ;
		case LC_EXTENDED:
			if (lem->lem_errors >= smc->s.lct_extended)
				phy->pc_lem_fail = TRUE ;
			break ;
		}
		DB_PCMN(1," >>errors : %d\n",lem->lem_errors,0) ;
	}
	if (phy->pc_lem_fail) {
		mib->fddiPORTLCTFail_Ct++ ;
		mib->fddiPORTLem_Reject_Ct++ ;
	}
	else
		mib->fddiPORTLCTFail_Ct = 0 ;
}

/*
 * LEM functions
 */
static void sm_ph_lem_start(struct s_smc *smc, int np, int threshold)
{
	struct lem_counter *lem = &smc->y[np].lem ;

	lem->lem_on = 1 ;
	lem->lem_errors = 0L ;

	/* Do NOT reset mib->fddiPORTLer_Estimate here. It is called too
	 * often.
	 */

	outpw(PLC(np,PL_LE_THRESHOLD),threshold) ;
	(void)inpw(PLC(np,PL_LINK_ERR_CTR)) ;	/* clear error counter */

	/* enable LE INT */
	SETMASK(PLC(np,PL_INTR_MASK),PL_LE_CTR,PL_LE_CTR) ;
}

static void sm_ph_lem_stop(struct s_smc *smc, int np)
{
	struct lem_counter *lem = &smc->y[np].lem ;

	lem->lem_on = 0 ;
	CLEAR(PLC(np,PL_INTR_MASK),PL_LE_CTR) ;
}

/* ARGSUSED */
void sm_pm_ls_latch(struct s_smc *smc, int phy, int on_off)
/* int on_off;	en- or disable ident. ls */
{
	SK_UNUSED(smc) ;

	phy = phy ; on_off = on_off ;
}


/*
 * PCM pseudo code
 * receive actions are called AFTER the bit n is received,
 * i.e. if pc_rcode_actions(5) is called, bit 6 is the next bit to be received
 */

/*
 * PCM pseudo code 5.1 .. 6.1
 */
static void pc_rcode_actions(struct s_smc *smc, int bit, struct s_phy *phy)
{
	struct fddi_mib_p	*mib ;

	mib = phy->mib ;

	DB_PCMN(1,"SIG rec %x %x:\n", bit,phy->r_val[bit] ) ;
	bit++ ;

	switch(bit) {
	case 0:
	case 1:
	case 2:
		break ;
	case 3 :
		if (phy->r_val[1] == 0 && phy->r_val[2] == 0)
			mib->fddiPORTNeighborType = TA ;
		else if (phy->r_val[1] == 0 && phy->r_val[2] == 1)
			mib->fddiPORTNeighborType = TB ;
		else if (phy->r_val[1] == 1 && phy->r_val[2] == 0)
			mib->fddiPORTNeighborType = TS ;
		else if (phy->r_val[1] == 1 && phy->r_val[2] == 1)
			mib->fddiPORTNeighborType = TM ;
		break ;
	case 4:
		if (mib->fddiPORTMy_Type == TM &&
			mib->fddiPORTNeighborType == TM) {
			DB_PCMN(1,"PCM %c : E100 withhold M-M\n",
				phy->phy_name,0) ;
			mib->fddiPORTPC_Withhold = PC_WH_M_M ;
			RS_SET(smc,RS_EVENT) ;
		}
		else if (phy->t_val[3] || phy->r_val[3]) {
			mib->fddiPORTPC_Withhold = PC_WH_NONE ;
			if (mib->fddiPORTMy_Type == TM ||
			    mib->fddiPORTNeighborType == TM)
				phy->pc_mode = PM_TREE ;
			else
				phy->pc_mode = PM_PEER ;

			/* reevaluate the selection criteria (wc_flag) */
			all_selection_criteria (smc);

			if (phy->wc_flag) {
				mib->fddiPORTPC_Withhold = PC_WH_PATH ;
			}
		}
		else {
			mib->fddiPORTPC_Withhold = PC_WH_OTHER ;
			RS_SET(smc,RS_EVENT) ;
			DB_PCMN(1,"PCM %c : E101 withhold other\n",
				phy->phy_name,0) ;
		}
		phy->twisted = ((mib->fddiPORTMy_Type != TS) &&
				(mib->fddiPORTMy_Type != TM) &&
				(mib->fddiPORTNeighborType ==
				mib->fddiPORTMy_Type)) ;
		if (phy->twisted) {
			DB_PCMN(1,"PCM %c : E102 !!! TWISTED !!!\n",
				phy->phy_name,0) ;
		}
		break ;
	case 5 :
		break ;
	case 6:
		if (phy->t_val[4] || phy->r_val[4]) {
			if ((phy->t_val[4] && phy->t_val[5]) ||
			    (phy->r_val[4] && phy->r_val[5]) )
				phy->lc_test = LC_EXTENDED ;
			else
				phy->lc_test = LC_LONG ;
		}
		else if (phy->t_val[5] || phy->r_val[5])
			phy->lc_test = LC_MEDIUM ;
		else
			phy->lc_test = LC_SHORT ;
		switch (phy->lc_test) {
		case LC_SHORT :				/* 50ms */
			outpw(PLC((int)phy->np,PL_LC_LENGTH), TP_LC_LENGTH ) ;
			phy->t_next[7] = smc->s.pcm_lc_short ;
			break ;
		case LC_MEDIUM :			/* 500ms */
			outpw(PLC((int)phy->np,PL_LC_LENGTH), TP_LC_LONGLN ) ;
			phy->t_next[7] = smc->s.pcm_lc_medium ;
			break ;
		case LC_LONG :
			SETMASK(PLC((int)phy->np,PL_CNTRL_B),PL_LONG,PL_LONG) ;
			phy->t_next[7] = smc->s.pcm_lc_long ;
			break ;
		case LC_EXTENDED :
			SETMASK(PLC((int)phy->np,PL_CNTRL_B),PL_LONG,PL_LONG) ;
			phy->t_next[7] = smc->s.pcm_lc_extended ;
			break ;
		}
		if (phy->t_next[7] > smc->s.pcm_lc_medium) {
			start_pcm_timer0(smc,phy->t_next[7],PC_TIMEOUT_LCT,phy);
		}
		DB_PCMN(1,"LCT timer = %ld us\n", phy->t_next[7], 0) ;
		phy->t_next[9] = smc->s.pcm_t_next_9 ;
		break ;
	case 7:
		if (phy->t_val[6]) {
			phy->cf_loop = TRUE ;
		}
		phy->td_flag = TRUE ;
		break ;
	case 8:
		if (phy->t_val[7] || phy->r_val[7]) {
			DB_PCMN(1,"PCM %c : E103 LCT fail %s\n",
				phy->phy_name,phy->t_val[7]? "local":"remote") ;
			queue_event(smc,(int)(EVENT_PCM+phy->np),PC_START) ;
		}
		break ;
	case 9:
		if (phy->t_val[8] || phy->r_val[8]) {
			if (phy->t_val[8])
				phy->cf_loop = TRUE ;
			phy->td_flag = TRUE ;
		}
		break ;
	case 10:
		if (phy->r_val[9]) {
			/* neighbor intends to have MAC on output */ ;
			mib->fddiPORTMacIndicated.R_val = TRUE ;
		}
		else {
			/* neighbor does not intend to have MAC on output */ ;
			mib->fddiPORTMacIndicated.R_val = FALSE ;
		}
		break ;
	}
}

/*
 * PCM pseudo code 5.1 .. 6.1
 */
static void pc_tcode_actions(struct s_smc *smc, const int bit, struct s_phy *phy)
{
	int	np = phy->np ;
	struct fddi_mib_p	*mib ;

	mib = phy->mib ;

	switch(bit) {
	case 0:
		phy->t_val[0] = 0 ;		/* no escape used */
		break ;
	case 1:
		if (mib->fddiPORTMy_Type == TS || mib->fddiPORTMy_Type == TM)
			phy->t_val[1] = 1 ;
		else
			phy->t_val[1] = 0 ;
		break ;
	case 2 :
		if (mib->fddiPORTMy_Type == TB || mib->fddiPORTMy_Type == TM)
			phy->t_val[2] = 1 ;
		else
			phy->t_val[2] = 0 ;
		break ;
	case 3:
		{
		int	type,ne ;
		int	policy ;

		type = mib->fddiPORTMy_Type ;
		ne = mib->fddiPORTNeighborType ;
		policy = smc->mib.fddiSMTConnectionPolicy ;

		phy->t_val[3] = 1 ;	/* Accept connection */
		switch (type) {
		case TA :
			if (
				((policy & POLICY_AA) && ne == TA) ||
				((policy & POLICY_AB) && ne == TB) ||
				((policy & POLICY_AS) && ne == TS) ||
				((policy & POLICY_AM) && ne == TM) )
				phy->t_val[3] = 0 ;	/* Reject */
			break ;
		case TB :
			if (
				((policy & POLICY_BA) && ne == TA) ||
				((policy & POLICY_BB) && ne == TB) ||
				((policy & POLICY_BS) && ne == TS) ||
				((policy & POLICY_BM) && ne == TM) )
				phy->t_val[3] = 0 ;	/* Reject */
			break ;
		case TS :
			if (
				((policy & POLICY_SA) && ne == TA) ||
				((policy & POLICY_SB) && ne == TB) ||
				((policy & POLICY_SS) && ne == TS) ||
				((policy & POLICY_SM) && ne == TM) )
				phy->t_val[3] = 0 ;	/* Reject */
			break ;
		case TM :
			if (	ne == TM ||
				((policy & POLICY_MA) && ne == TA) ||
				((policy & POLICY_MB) && ne == TB) ||
				((policy & POLICY_MS) && ne == TS) ||
				((policy & POLICY_MM) && ne == TM) )
				phy->t_val[3] = 0 ;	/* Reject */
			break ;
		}
#ifndef	SLIM_SMT
		/*
		 * detect undesirable connection attempt event
		 */
		if (	(type == TA && ne == TA ) ||
			(type == TA && ne == TS ) ||
			(type == TB && ne == TB ) ||
			(type == TB && ne == TS ) ||
			(type == TS && ne == TA ) ||
			(type == TS && ne == TB ) ) {
			smt_srf_event(smc,SMT_EVENT_PORT_CONNECTION,
				(int) (INDEX_PORT+ phy->np) ,0) ;
		}
#endif
		}
		break ;
	case 4:
		if (mib->fddiPORTPC_Withhold == PC_WH_NONE) {
			if (phy->pc_lem_fail) {
				phy->t_val[4] = 1 ;	/* long */
				phy->t_val[5] = 0 ;
			}
			else {
				phy->t_val[4] = 0 ;
				if (mib->fddiPORTLCTFail_Ct > 0)
					phy->t_val[5] = 1 ;	/* medium */
				else
					phy->t_val[5] = 0 ;	/* short */

				/*
				 * Implementers choice: use medium
				 * instead of short when undesired
				 * connection attempt is made.
				 */
				if (phy->wc_flag)
					phy->t_val[5] = 1 ;	/* medium */
			}
			mib->fddiPORTConnectState = PCM_CONNECTING ;
		}
		else {
			mib->fddiPORTConnectState = PCM_STANDBY ;
			phy->t_val[4] = 1 ;	/* extended */
			phy->t_val[5] = 1 ;
		}
		break ;
	case 5:
		break ;
	case 6:
		/* we do NOT have a MAC for LCT */
		phy->t_val[6] = 0 ;
		break ;
	case 7:
		phy->cf_loop = FALSE ;
		lem_check_lct(smc,phy) ;
		if (phy->pc_lem_fail) {
			DB_PCMN(1,"PCM %c : E104 LCT failed\n",
				phy->phy_name,0) ;
			phy->t_val[7] = 1 ;
		}
		else
			phy->t_val[7] = 0 ;
		break ;
	case 8:
		phy->t_val[8] = 0 ;	/* Don't request MAC loopback */
		break ;
	case 9:
		phy->cf_loop = 0 ;
		if ((mib->fddiPORTPC_Withhold != PC_WH_NONE) ||
		     ((smc->s.sas == SMT_DAS) && (phy->wc_flag))) {
			queue_event(smc,EVENT_PCM+np,PC_START) ;
			break ;
		}
		phy->t_val[9] = FALSE ;
		switch (smc->s.sas) {
		case SMT_DAS :
			/*
			 * MAC intended on output
			 */
			if (phy->pc_mode == PM_TREE) {
				if ((np == PB) || ((np == PA) &&
				(smc->y[PB].mib->fddiPORTConnectState !=
					PCM_ACTIVE)))
					phy->t_val[9] = TRUE ;
			}
			else {
				if (np == PB)
					phy->t_val[9] = TRUE ;
			}
			break ;
		case SMT_SAS :
			if (np == PS)
				phy->t_val[9] = TRUE ;
			break ;
#ifdef	CONCENTRATOR
		case SMT_NAC :
			/*
			 * MAC intended on output
			 */
			if (np == PB)
				phy->t_val[9] = TRUE ;
			break ;
#endif
		}
		mib->fddiPORTMacIndicated.T_val = phy->t_val[9] ;
		break ;
	}
	DB_PCMN(1,"SIG snd %x %x:\n", bit,phy->t_val[bit] ) ;
}

/*
 * return status twisted (called by SMT)
 */
int pcm_status_twisted(struct s_smc *smc)
{
	int	twist = 0 ;
	if (smc->s.sas != SMT_DAS)
		return 0;
	if (smc->y[PA].twisted && (smc->y[PA].mib->fddiPORTPCMState == PC8_ACTIVE))
		twist |= 1 ;
	if (smc->y[PB].twisted && (smc->y[PB].mib->fddiPORTPCMState == PC8_ACTIVE))
		twist |= 2 ;
	return twist;
}

/*
 * return status	(called by SMT)
 *	type
 *	state
 *	remote phy type
 *	remote mac yes/no
 */
void pcm_status_state(struct s_smc *smc, int np, int *type, int *state,
		      int *remote, int *mac)
{
	struct s_phy	*phy = &smc->y[np] ;
	struct fddi_mib_p	*mib ;

	mib = phy->mib ;

	/* remote PHY type and MAC - set only if active */
	*mac = 0 ;
	*type = mib->fddiPORTMy_Type ;		/* our PHY type */
	*state = mib->fddiPORTConnectState ;
	*remote = mib->fddiPORTNeighborType ;

	switch(mib->fddiPORTPCMState) {
	case PC8_ACTIVE :
		*mac = mib->fddiPORTMacIndicated.R_val ;
		break ;
	}
}

/*
 * return rooted station status (called by SMT)
 */
int pcm_rooted_station(struct s_smc *smc)
{
	int	n ;

	for (n = 0 ; n < NUMPHYS ; n++) {
		if (smc->y[n].mib->fddiPORTPCMState == PC8_ACTIVE &&
		    smc->y[n].mib->fddiPORTNeighborType == TM)
			return 0;
	}
	return 1;
}

/*
 * Interrupt actions for PLC & PCM events
 */
void plc_irq(struct s_smc *smc, int np, unsigned int cmd)
/* int np;	PHY index */
{
	struct s_phy *phy = &smc->y[np] ;
	struct s_plc *plc = &phy->plc ;
	int		n ;
#ifdef	SUPERNET_3
	int		corr_mask ;
#endif	/* SUPERNET_3 */
	int		i ;

	if (np >= smc->s.numphys) {
		plc->soft_err++ ;
		return ;
	}
	if (cmd & PL_EBUF_ERR) {	/* elastic buff. det. over-|underflow*/
		/*
		 * Check whether the SRF Condition occurred.
		 */
		if (!plc->ebuf_cont && phy->mib->fddiPORTPCMState == PC8_ACTIVE){
			/*
			 * This is the real Elasticity Error.
			 * More than one in a row are treated as a
			 * single one.
			 * Only count this in the active state.
			 */
			phy->mib->fddiPORTEBError_Ct ++ ;

		}

		plc->ebuf_err++ ;
		if (plc->ebuf_cont <= 1000) {
			/*
			 * Prevent counter from being wrapped after
			 * hanging years in that interrupt.
			 */
			plc->ebuf_cont++ ;	/* Ebuf continuous error */
		}

#ifdef	SUPERNET_3
		if (plc->ebuf_cont == 1000 &&
			((inpw(PLC(np,PL_STATUS_A)) & PLC_REV_MASK) ==
			PLC_REV_SN3)) {
			/*
			 * This interrupt remeained high for at least
			 * 1000 consecutive interrupt calls.
			 *
			 * This is caused by a hardware error of the
			 * ORION part of the Supernet III chipset.
			 *
			 * Disable this bit from the mask.
			 */
			corr_mask = (plc_imsk_na & ~PL_EBUF_ERR) ;
			outpw(PLC(np,PL_INTR_MASK),corr_mask);

			/*
			 * Disconnect from the ring.
			 * Call the driver with the reset indication.
			 */
			queue_event(smc,EVENT_ECM,EC_DISCONNECT) ;

			/*
			 * Make an error log entry.
			 */
			SMT_ERR_LOG(smc,SMT_E0136, SMT_E0136_MSG) ;

			/*
			 * Indicate the Reset.
			 */
			drv_reset_indication(smc) ;
		}
#endif	/* SUPERNET_3 */
	} else {
		/* Reset the continuous error variable */
		plc->ebuf_cont = 0 ;	/* reset Ebuf continuous error */
	}
	if (cmd & PL_PHYINV) {		/* physical layer invalid signal */
		plc->phyinv++ ;
	}
	if (cmd & PL_VSYM_CTR) {	/* violation symbol counter has incr.*/
		plc->vsym_ctr++ ;
	}
	if (cmd & PL_MINI_CTR) {	/* dep. on PLC_CNTRL_A's MINI_CTR_INT*/
		plc->mini_ctr++ ;
	}
	if (cmd & PL_LE_CTR) {		/* link error event counter */
		int	j ;

		/*
		 * note: PL_LINK_ERR_CTR MUST be read to clear it
		 */
		j = inpw(PLC(np,PL_LE_THRESHOLD)) ;
		i = inpw(PLC(np,PL_LINK_ERR_CTR)) ;

		if (i < j) {
			/* wrapped around */
			i += 256 ;
		}

		if (phy->lem.lem_on) {
			/* Note: Lem errors shall only be counted when
			 * link is ACTIVE or LCT is active.
			 */
			phy->lem.lem_errors += i ;
			phy->mib->fddiPORTLem_Ct += i ;
		}
	}
	if (cmd & PL_TPC_EXPIRED) {	/* TPC timer reached zero */
		if (plc->p_state == PS_LCT) {
			/*
			 * end of LCT
			 */
			;
		}
		plc->tpc_exp++ ;
	}
	if (cmd & PL_LS_MATCH) {	/* LS == LS in PLC_CNTRL_B's MATCH_LS*/
		switch (inpw(PLC(np,PL_CNTRL_B)) & PL_MATCH_LS) {
		case PL_I_IDLE :	phy->curr_ls = PC_ILS ;		break ;
		case PL_I_HALT :	phy->curr_ls = PC_HLS ;		break ;
		case PL_I_MASTR :	phy->curr_ls = PC_MLS ;		break ;
		case PL_I_QUIET :	phy->curr_ls = PC_QLS ;		break ;
		}
	}
	if (cmd & PL_PCM_BREAK) {	/* PCM has entered the BREAK state */
		int	reason;

		reason = inpw(PLC(np,PL_STATUS_B)) & PL_BREAK_REASON ;

		switch (reason) {
		case PL_B_PCS :		plc->b_pcs++ ;	break ;
		case PL_B_TPC :		plc->b_tpc++ ;	break ;
		case PL_B_TNE :		plc->b_tne++ ;	break ;
		case PL_B_QLS :		plc->b_qls++ ;	break ;
		case PL_B_ILS :		plc->b_ils++ ;	break ;
		case PL_B_HLS :		plc->b_hls++ ;	break ;
		}

		/*jd 05-Aug-1999 changed: Bug #10419 */
		DB_PCMN(1,"PLC %d: MDcF = %x\n", np, smc->e.DisconnectFlag);
		if (smc->e.DisconnectFlag == FALSE) {
			DB_PCMN(1,"PLC %d: restart (reason %x)\n", np, reason);
			queue_event(smc,EVENT_PCM+np,PC_START) ;
		}
		else {
			DB_PCMN(1,"PLC %d: NO!! restart (reason %x)\n", np, reason);
		}
		return ;
	}
	/*
	 * If both CODE & ENABLE are set ignore enable
	 */
	if (cmd & PL_PCM_CODE) { /* receive last sign.-bit | LCT complete */
		queue_event(smc,EVENT_PCM+np,PC_SIGNAL) ;
		n = inpw(PLC(np,PL_RCV_VECTOR)) ;
		for (i = 0 ; i < plc->p_bits ; i++) {
			phy->r_val[plc->p_start+i] = n & 1 ;
			n >>= 1 ;
		}
	}
	else if (cmd & PL_PCM_ENABLED) { /* asserted SC_JOIN, scrub.completed*/
		queue_event(smc,EVENT_PCM+np,PC_JOIN) ;
	}
	if (cmd & PL_TRACE_PROP) {	/* MLS while PC8_ACTIV || PC2_TRACE */
		/*PC22b*/
		if (!phy->tr_flag) {
			DB_PCMN(1,"PCM : irq TRACE_PROP %d %d\n",
				np,smc->mib.fddiSMTECMState) ;
			phy->tr_flag = TRUE ;
			smc->e.trace_prop |= ENTITY_BIT(ENTITY_PHY(np)) ;
			queue_event(smc,EVENT_ECM,EC_TRACE_PROP) ;
		}
	}
	/*
	 * filter PLC glitch ???
	 * QLS || HLS only while in PC2_TRACE state
	 */
	if ((cmd & PL_SELF_TEST) && (phy->mib->fddiPORTPCMState == PC2_TRACE)) {
		/*PC22a*/
		if (smc->e.path_test == PT_PASSED) {
			DB_PCMN(1,"PCM : state = %s %d\n", get_pcmstate(smc,np),
				phy->mib->fddiPORTPCMState) ;

			smc->e.path_test = PT_PENDING ;
			queue_event(smc,EVENT_ECM,EC_PATH_TEST) ;
		}
	}
	if (cmd & PL_TNE_EXPIRED) {	/* TNE: length of noise events */
		/* break_required (TNE > NS_Max) */
		if (phy->mib->fddiPORTPCMState == PC8_ACTIVE) {
			if (!phy->tr_flag) {
			   DB_PCMN(1,"PCM %c : PC81 %s\n",phy->phy_name,"NSE");
			   queue_event(smc,EVENT_PCM+np,PC_START) ;
			   return ;
			}
		}
	}
#if	0
	if (cmd & PL_NP_ERR) {		/* NP has requested to r/w an inv reg*/
		/*
		 * It's a bug by AMD
		 */
		plc->np_err++ ;
	}
	/* pin inactiv (GND) */
	if (cmd & PL_PARITY_ERR) {	/* p. error dedected on TX9-0 inp */
		plc->parity_err++ ;
	}
	if (cmd & PL_LSDO) {		/* carrier detected */
		;
	}
#endif
}

#ifdef	DEBUG
/*
 * fill state struct
 */
void pcm_get_state(struct s_smc *smc, struct smt_state *state)
{
	struct s_phy	*phy ;
	struct pcm_state *pcs ;
	int	i ;
	int	ii ;
	short	rbits ;
	short	tbits ;
	struct fddi_mib_p	*mib ;

	for (i = 0, phy = smc->y, pcs = state->pcm_state ; i < NUMPHYS ;
		i++ , phy++, pcs++ ) {
		mib = phy->mib ;
		pcs->pcm_type = (u_char) mib->fddiPORTMy_Type ;
		pcs->pcm_state = (u_char) mib->fddiPORTPCMState ;
		pcs->pcm_mode = phy->pc_mode ;
		pcs->pcm_neighbor = (u_char) mib->fddiPORTNeighborType ;
		pcs->pcm_bsf = mib->fddiPORTBS_Flag ;
		pcs->pcm_lsf = phy->ls_flag ;
		pcs->pcm_lct_fail = (u_char) mib->fddiPORTLCTFail_Ct ;
		pcs->pcm_ls_rx = LS2MIB(sm_pm_get_ls(smc,i)) ;
		for (ii = 0, rbits = tbits = 0 ; ii < NUMBITS ; ii++) {
			rbits <<= 1 ;
			tbits <<= 1 ;
			if (phy->r_val[NUMBITS-1-ii])
				rbits |= 1 ;
			if (phy->t_val[NUMBITS-1-ii])
				tbits |= 1 ;
		}
		pcs->pcm_r_val = rbits ;
		pcs->pcm_t_val = tbits ;
	}
}

int get_pcm_state(struct s_smc *smc, int np)
{
	int pcs ;

	SK_UNUSED(smc) ;

	switch (inpw(PLC(np,PL_STATUS_B)) & PL_PCM_STATE) {
		case PL_PC0 :	pcs = PC_STOP ;		break ;
		case PL_PC1 :	pcs = PC_START ;	break ;
		case PL_PC2 :	pcs = PC_TRACE ;	break ;
		case PL_PC3 :	pcs = PC_SIGNAL ;	break ;
		case PL_PC4 :	pcs = PC_SIGNAL ;	break ;
		case PL_PC5 :	pcs = PC_SIGNAL ;	break ;
		case PL_PC6 :	pcs = PC_JOIN ;		break ;
		case PL_PC7 :	pcs = PC_JOIN ;		break ;
		case PL_PC8 :	pcs = PC_ENABLE ;	break ;
		case PL_PC9 :	pcs = PC_MAINT ;	break ;
		default :	pcs = PC_DISABLE ; 	break ;
	}
	return pcs;
}

char *get_linestate(struct s_smc *smc, int np)
{
	char *ls = "" ;

	SK_UNUSED(smc) ;

	switch (inpw(PLC(np,PL_STATUS_A)) & PL_LINE_ST) {
		case PL_L_NLS :	ls = "NOISE" ;	break ;
		case PL_L_ALS :	ls = "ACTIV" ;	break ;
		case PL_L_UND :	ls = "UNDEF" ;	break ;
		case PL_L_ILS4:	ls = "ILS 4" ;	break ;
		case PL_L_QLS :	ls = "QLS" ;	break ;
		case PL_L_MLS :	ls = "MLS" ;	break ;
		case PL_L_HLS :	ls = "HLS" ;	break ;
		case PL_L_ILS16:ls = "ILS16" ;	break ;
#ifdef	lint
		default:	ls = "unknown" ; break ;
#endif
	}
	return ls;
}

char *get_pcmstate(struct s_smc *smc, int np)
{
	char *pcs ;
	
	SK_UNUSED(smc) ;

	switch (inpw(PLC(np,PL_STATUS_B)) & PL_PCM_STATE) {
		case PL_PC0 :	pcs = "OFF" ;		break ;
		case PL_PC1 :	pcs = "BREAK" ;		break ;
		case PL_PC2 :	pcs = "TRACE" ;		break ;
		case PL_PC3 :	pcs = "CONNECT";	break ;
		case PL_PC4 :	pcs = "NEXT" ;		break ;
		case PL_PC5 :	pcs = "SIGNAL" ;	break ;
		case PL_PC6 :	pcs = "JOIN" ;		break ;
		case PL_PC7 :	pcs = "VERIFY" ;	break ;
		case PL_PC8 :	pcs = "ACTIV" ;		break ;
		case PL_PC9 :	pcs = "MAINT" ;		break ;
		default :	pcs = "UNKNOWN" ; 	break ;
	}
	return pcs;
}

void list_phy(struct s_smc *smc)
{
	struct s_plc *plc ;
	int np ;

	for (np = 0 ; np < NUMPHYS ; np++) {
		plc  = &smc->y[np].plc ;
		printf("PHY %d:\tERRORS\t\t\tBREAK_REASONS\t\tSTATES:\n",np) ;
		printf("\tsoft_error: %ld \t\tPC_Start : %ld\n",
						plc->soft_err,plc->b_pcs);
		printf("\tparity_err: %ld \t\tTPC exp. : %ld\t\tLine: %s\n",
			plc->parity_err,plc->b_tpc,get_linestate(smc,np)) ;
		printf("\tebuf_error: %ld \t\tTNE exp. : %ld\n",
						plc->ebuf_err,plc->b_tne) ;
		printf("\tphyinvalid: %ld \t\tQLS det. : %ld\t\tPCM : %s\n",
			plc->phyinv,plc->b_qls,get_pcmstate(smc,np)) ;
		printf("\tviosym_ctr: %ld \t\tILS det. : %ld\n",
						plc->vsym_ctr,plc->b_ils)  ;
		printf("\tmingap_ctr: %ld \t\tHLS det. : %ld\n",
						plc->mini_ctr,plc->b_hls) ;
		printf("\tnodepr_err: %ld\n",plc->np_err) ;
		printf("\tTPC_exp : %ld\n",plc->tpc_exp) ;
		printf("\tLEM_err : %ld\n",smc->y[np].lem.lem_errors) ;
	}
}


#ifdef	CONCENTRATOR
void pcm_lem_dump(struct s_smc *smc)
{
	int		i ;
	struct s_phy	*phy ;
	struct fddi_mib_p	*mib ;

	char		*entostring() ;

	printf("PHY	errors	BER\n") ;
	printf("----------------------\n") ;
	for (i = 0,phy = smc->y ; i < NUMPHYS ; i++,phy++) {
		if (!plc_is_installed(smc,i))
			continue ;
		mib = phy->mib ;
		printf("%s\t%ld\t10E-%d\n",
			entostring(smc,ENTITY_PHY(i)),
			mib->fddiPORTLem_Ct,
			mib->fddiPORTLer_Estimate) ;
	}
}
#endif
#endif
