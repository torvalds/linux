// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	See the file "skfddi.c" for further information.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
	SMT 7.2 Status Response Frame Implementation
	SRF state machine and frame generation
*/

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"
#include "h/smt_p.h"

#define KERNEL
#include "h/smtstate.h"

#ifndef	SLIM_SMT
#ifndef	BOOT

/*
 * function declarations
 */
static void clear_all_rep(struct s_smc *smc);
static void clear_reported(struct s_smc *smc);
static void smt_send_srf(struct s_smc *smc);
static struct s_srf_evc *smt_get_evc(struct s_smc *smc, int code, int index);

#define MAX_EVCS	ARRAY_SIZE(smc->evcs)

struct evc_init {
	u_char code ;
	u_char index ;
	u_char n ;
	u_short	para ;
}  ;

static const struct evc_init evc_inits[] = {
	{ SMT_COND_SMT_PEER_WRAP,		0,1,SMT_P1048	} ,

	{ SMT_COND_MAC_DUP_ADDR,		INDEX_MAC, NUMMACS,SMT_P208C } ,
	{ SMT_COND_MAC_FRAME_ERROR,		INDEX_MAC, NUMMACS,SMT_P208D } ,
	{ SMT_COND_MAC_NOT_COPIED,		INDEX_MAC, NUMMACS,SMT_P208E } ,
	{ SMT_EVENT_MAC_NEIGHBOR_CHANGE,	INDEX_MAC, NUMMACS,SMT_P208F } ,
	{ SMT_EVENT_MAC_PATH_CHANGE,		INDEX_MAC, NUMMACS,SMT_P2090 } ,

	{ SMT_COND_PORT_LER,			INDEX_PORT,NUMPHYS,SMT_P4050 } ,
	{ SMT_COND_PORT_EB_ERROR,		INDEX_PORT,NUMPHYS,SMT_P4052 } ,
	{ SMT_EVENT_PORT_CONNECTION,		INDEX_PORT,NUMPHYS,SMT_P4051 } ,
	{ SMT_EVENT_PORT_PATH_CHANGE,		INDEX_PORT,NUMPHYS,SMT_P4053 } ,
} ;

#define MAX_INIT_EVC	ARRAY_SIZE(evc_inits)

void smt_init_evc(struct s_smc *smc)
{
	struct s_srf_evc	*evc ;
	const struct evc_init 	*init ;
	unsigned int		i ;
	int			index ;
	int			offset ;

	static u_char		fail_safe = FALSE ;

	memset((char *)smc->evcs,0,sizeof(smc->evcs)) ;

	evc = smc->evcs ;
	init = evc_inits ;

	for (i = 0 ; i < MAX_INIT_EVC ; i++) {
		for (index = 0 ; index < init->n ; index++) {
			evc->evc_code = init->code ;
			evc->evc_para = init->para ;
			evc->evc_index = init->index + index ;
#ifndef	DEBUG
			evc->evc_multiple = &fail_safe ;
			evc->evc_cond_state = &fail_safe ;
#endif
			evc++ ;
		}
		init++ ;
	}

	if ((unsigned int) (evc - smc->evcs) > MAX_EVCS) {
		SMT_PANIC(smc,SMT_E0127, SMT_E0127_MSG) ;
	}

	/*
	 * conditions
	 */
	smc->evcs[0].evc_cond_state = &smc->mib.fddiSMTPeerWrapFlag ;
	smc->evcs[1].evc_cond_state =
		&smc->mib.m[MAC0].fddiMACDuplicateAddressCond ;
	smc->evcs[2].evc_cond_state =
		&smc->mib.m[MAC0].fddiMACFrameErrorFlag ;
	smc->evcs[3].evc_cond_state =
		&smc->mib.m[MAC0].fddiMACNotCopiedFlag ;

	/*
	 * events
	 */
	smc->evcs[4].evc_multiple = &smc->mib.m[MAC0].fddiMACMultiple_N ;
	smc->evcs[5].evc_multiple = &smc->mib.m[MAC0].fddiMACMultiple_P ;

	offset = 6 ;
	for (i = 0 ; i < NUMPHYS ; i++) {
		/*
		 * conditions
		 */
		smc->evcs[offset + 0*NUMPHYS].evc_cond_state =
			&smc->mib.p[i].fddiPORTLerFlag ;
		smc->evcs[offset + 1*NUMPHYS].evc_cond_state =
			&smc->mib.p[i].fddiPORTEB_Condition ;

		/*
		 * events
		 */
		smc->evcs[offset + 2*NUMPHYS].evc_multiple =
			&smc->mib.p[i].fddiPORTMultiple_U ;
		smc->evcs[offset + 3*NUMPHYS].evc_multiple =
			&smc->mib.p[i].fddiPORTMultiple_P ;
		offset++ ;
	}
#ifdef	DEBUG
	for (i = 0, evc = smc->evcs ; i < MAX_EVCS ; i++, evc++) {
		if (SMT_IS_CONDITION(evc->evc_code)) {
			if (!evc->evc_cond_state) {
				SMT_PANIC(smc,SMT_E0128, SMT_E0128_MSG) ;
			}
			evc->evc_multiple = &fail_safe ;
		}
		else {
			if (!evc->evc_multiple) {
				SMT_PANIC(smc,SMT_E0129, SMT_E0129_MSG) ;
			}
			evc->evc_cond_state = &fail_safe ;
		}
	}
#endif
	smc->srf.TSR = smt_get_time() ;
	smc->srf.sr_state = SR0_WAIT ;
}

static struct s_srf_evc *smt_get_evc(struct s_smc *smc, int code, int index)
{
	unsigned int		i ;
	struct s_srf_evc	*evc ;

	for (i = 0, evc = smc->evcs ; i < MAX_EVCS ; i++, evc++) {
		if (evc->evc_code == code && evc->evc_index == index)
			return evc;
	}
	return NULL;
}

#define THRESHOLD_2	(2*TICKS_PER_SECOND)
#define THRESHOLD_32	(32*TICKS_PER_SECOND)

static const char * const srf_names[] = {
	"None","MACPathChangeEvent",	"MACNeighborChangeEvent",
	"PORTPathChangeEvent",		"PORTUndesiredConnectionAttemptEvent",
	"SMTPeerWrapCondition",		"SMTHoldCondition",
	"MACFrameErrorCondition",	"MACDuplicateAddressCondition",
	"MACNotCopiedCondition",	"PORTEBErrorCondition",
	"PORTLerCondition"
} ;

void smt_srf_event(struct s_smc *smc, int code, int index, int cond)
{
	struct s_srf_evc	*evc ;
	int			cond_asserted = 0 ;
	int			cond_deasserted = 0 ;
	int			event_occurred = 0 ;
	int			tsr ;
	int			T_Limit = 2*TICKS_PER_SECOND ;

	if (code == SMT_COND_MAC_DUP_ADDR && cond) {
		RS_SET(smc,RS_DUPADDR) ;
	}

	if (code) {
		DB_SMT("SRF: %s index %d", srf_names[code], index);

		if (!(evc = smt_get_evc(smc,code,index))) {
			DB_SMT("SRF : smt_get_evc() failed");
			return ;
		}
		/*
		 * ignore condition if no change
		 */
		if (SMT_IS_CONDITION(code)) {
			if (*evc->evc_cond_state == cond)
				return ;
		}

		/*
		 * set transition time stamp
		 */
		smt_set_timestamp(smc,smc->mib.fddiSMTTransitionTimeStamp) ;
		if (SMT_IS_CONDITION(code)) {
			DB_SMT("SRF: condition is %s", cond ? "ON" : "OFF");
			if (cond) {
				*evc->evc_cond_state = TRUE ;
				evc->evc_rep_required = TRUE ;
				smc->srf.any_report = TRUE ;
				cond_asserted = TRUE ;
			}
			else {
				*evc->evc_cond_state = FALSE ;
				cond_deasserted = TRUE ;
			}
		}
		else {
			if (evc->evc_rep_required) {
				*evc->evc_multiple  = TRUE ;
			}
			else {
				evc->evc_rep_required = TRUE ;
				*evc->evc_multiple  = FALSE ;
			}
			smc->srf.any_report = TRUE ;
			event_occurred = TRUE ;
		}
#ifdef	FDDI_MIB
		snmp_srf_event(smc,evc) ;
#endif	/* FDDI_MIB */
	}
	tsr = smt_get_time() - smc->srf.TSR ;

	switch (smc->srf.sr_state) {
	case SR0_WAIT :
		/* SR01a */
		if (cond_asserted && tsr < T_Limit) {
			smc->srf.SRThreshold = THRESHOLD_2 ;
			smc->srf.sr_state = SR1_HOLDOFF ;
			break ;
		}
		/* SR01b */
		if (cond_deasserted && tsr < T_Limit) {
			smc->srf.sr_state = SR1_HOLDOFF ;
			break ;
		}
		/* SR01c */
		if (event_occurred && tsr < T_Limit) {
			smc->srf.sr_state = SR1_HOLDOFF ;
			break ;
		}
		/* SR00b */
		if (cond_asserted && tsr >= T_Limit) {
			smc->srf.SRThreshold = THRESHOLD_2 ;
			smc->srf.TSR = smt_get_time() ;
			smt_send_srf(smc) ;
			break ;
		}
		/* SR00c */
		if (cond_deasserted && tsr >= T_Limit) {
			smc->srf.TSR = smt_get_time() ;
			smt_send_srf(smc) ;
			break ;
		}
		/* SR00d */
		if (event_occurred && tsr >= T_Limit) {
			smc->srf.TSR = smt_get_time() ;
			smt_send_srf(smc) ;
			break ;
		}
		/* SR00e */
		if (smc->srf.any_report && (u_long) tsr >=
			smc->srf.SRThreshold) {
			smc->srf.SRThreshold *= 2 ;
			if (smc->srf.SRThreshold > THRESHOLD_32)
				smc->srf.SRThreshold = THRESHOLD_32 ;
			smc->srf.TSR = smt_get_time() ;
			smt_send_srf(smc) ;
			break ;
		}
		/* SR02 */
		if (!smc->mib.fddiSMTStatRptPolicy) {
			smc->srf.sr_state = SR2_DISABLED ;
			break ;
		}
		break ;
	case SR1_HOLDOFF :
		/* SR10b */
		if (tsr >= T_Limit) {
			smc->srf.sr_state = SR0_WAIT ;
			smc->srf.TSR = smt_get_time() ;
			smt_send_srf(smc) ;
			break ;
		}
		/* SR11a */
		if (cond_asserted) {
			smc->srf.SRThreshold = THRESHOLD_2 ;
		}
		/* SR11b */
		/* SR11c */
		/* handled above */
		/* SR12 */
		if (!smc->mib.fddiSMTStatRptPolicy) {
			smc->srf.sr_state = SR2_DISABLED ;
			break ;
		}
		break ;
	case SR2_DISABLED :
		if (smc->mib.fddiSMTStatRptPolicy) {
			smc->srf.sr_state = SR0_WAIT ;
			smc->srf.TSR = smt_get_time() ;
			smc->srf.SRThreshold = THRESHOLD_2 ;
			clear_all_rep(smc) ;
			break ;
		}
		break ;
	}
}

static void clear_all_rep(struct s_smc *smc)
{
	struct s_srf_evc	*evc ;
	unsigned int		i ;

	for (i = 0, evc = smc->evcs ; i < MAX_EVCS ; i++, evc++) {
		evc->evc_rep_required = FALSE ;
		if (SMT_IS_CONDITION(evc->evc_code))
			*evc->evc_cond_state = FALSE ;
	}
	smc->srf.any_report = FALSE ;
}

static void clear_reported(struct s_smc *smc)
{
	struct s_srf_evc	*evc ;
	unsigned int		i ;

	smc->srf.any_report = FALSE ;
	for (i = 0, evc = smc->evcs ; i < MAX_EVCS ; i++, evc++) {
		if (SMT_IS_CONDITION(evc->evc_code)) {
			if (*evc->evc_cond_state == FALSE)
				evc->evc_rep_required = FALSE ;
			else
				smc->srf.any_report = TRUE ;
		}
		else {
			evc->evc_rep_required = FALSE ;
			*evc->evc_multiple = FALSE ;
		}
	}
}

/*
 * build and send SMT SRF frame
 */
static void smt_send_srf(struct s_smc *smc)
{

	struct smt_header	*smt ;
	struct s_srf_evc	*evc ;
	SK_LOC_DECL(struct s_pcon,pcon) ;
	SMbuf			*mb ;
	unsigned int		i ;

	static const struct fddi_addr SMT_SRF_DA = {
		{ 0x80, 0x01, 0x43, 0x00, 0x80, 0x08 }
	} ;

	/*
	 * build SMT header
	 */
	if (!smc->r.sm_ma_avail)
		return ;
	if (!(mb = smt_build_frame(smc,SMT_SRF,SMT_ANNOUNCE,0)))
		return ;

	RS_SET(smc,RS_SOFTERROR) ;

	smt = smtod(mb, struct smt_header *) ;
	smt->smt_dest = SMT_SRF_DA ;		/* DA == SRF multicast */

	/*
	 * setup parameter status
	 */
	pcon.pc_len = SMT_MAX_INFO_LEN ;	/* max para length */
	pcon.pc_err = 0 ;			/* no error */
	pcon.pc_badset = 0 ;			/* no bad set count */
	pcon.pc_p = (void *) (smt + 1) ;	/* paras start here */

	smt_add_para(smc,&pcon,(u_short) SMT_P1033,0,0) ;
	smt_add_para(smc,&pcon,(u_short) SMT_P1034,0,0) ;

	for (i = 0, evc = smc->evcs ; i < MAX_EVCS ; i++, evc++) {
		if (evc->evc_rep_required) {
			smt_add_para(smc,&pcon,evc->evc_para,
				(int)evc->evc_index,0) ;
		}
	}
	smt->smt_len = SMT_MAX_INFO_LEN - pcon.pc_len ;
	mb->sm_len = smt->smt_len + sizeof(struct smt_header) ;

	DB_SMT("SRF: sending SRF at %p, len %d", smt, mb->sm_len);
	DB_SMT("SRF: state SR%d Threshold %lu",
	       smc->srf.sr_state, smc->srf.SRThreshold / TICKS_PER_SECOND);
#ifdef	DEBUG
	dump_smt(smc,smt,"SRF Send") ;
#endif
	smt_send_frame(smc,mb,FC_SMT_INFO,0) ;
	clear_reported(smc) ;
}

#endif	/* no BOOT */
#endif	/* no SLIM_SMT */

