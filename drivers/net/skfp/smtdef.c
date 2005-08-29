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
	SMT/CMT defaults
*/

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"

#ifndef OEM_USER_DATA
#define OEM_USER_DATA	"SK-NET FDDI V2.0 Userdata"
#endif

#ifndef	lint
static const char ID_sccs[] = "@(#)smtdef.c	2.53 99/08/11 (C) SK " ;
#endif

/*
 * defaults
 */
#define TTMS(x)	((u_long)(x)*1000L)
#define TTS(x)	((u_long)(x)*1000000L)
#define TTUS(x)	((u_long)(x))

#define DEFAULT_TB_MIN		TTMS(5)
#define DEFAULT_TB_MAX		TTMS(50)
#define DEFAULT_C_MIN		TTUS(1600)
#define DEFAULT_T_OUT		TTMS(100+5)
#define DEFAULT_TL_MIN		TTUS(30)
#define DEFAULT_LC_SHORT	TTMS(50+5)
#define DEFAULT_LC_MEDIUM	TTMS(500+20)
#define DEFAULT_LC_LONG		TTS(5)+TTMS(50)
#define DEFAULT_LC_EXTENDED	TTS(50)+TTMS(50)
#define DEFAULT_T_NEXT_9	TTMS(200+10)
#define DEFAULT_NS_MAX		TTUS(1310)
#define DEFAULT_I_MAX		TTMS(25)
#define DEFAULT_IN_MAX		TTMS(40)
#define DEFAULT_TD_MIN		TTMS(5)
#define DEFAULT_T_NON_OP	TTS(1)
#define DEFAULT_T_STUCK		TTS(8)
#define DEFAULT_T_DIRECT	TTMS(370)
#define DEFAULT_T_JAM		TTMS(370)
#define DEFAULT_T_ANNOUNCE	TTMS(2500)
#define DEFAULT_D_MAX		TTUS(1617)
#define DEFAULT_LEM_ALARM	(8)
#define DEFAULT_LEM_CUTOFF	(7)
#define DEFAULT_TEST_DONE	TTS(1)
#define DEFAULT_CHECK_POLL	TTS(1)
#define DEFAULT_POLL		TTMS(50)

/*
 * LCT errors threshold
 */
#define DEFAULT_LCT_SHORT	1
#define DEFAULT_LCT_MEDIUM	3
#define DEFAULT_LCT_LONG	5
#define DEFAULT_LCT_EXTEND	50

/* Forward declarations */
void smt_reset_defaults(struct s_smc *smc, int level);
static void smt_init_mib(struct s_smc *smc, int level);
static int set_min_max(int maxflag, u_long mib, u_long limit, u_long *oper);

#define MS2BCLK(x)	((x)*12500L)
#define US2BCLK(x)	((x)*1250L)

void smt_reset_defaults(struct s_smc *smc, int level)
{
	struct smt_config	*smt ;
	int			i ;
	u_long			smt_boot_time;


	smt_init_mib(smc,level) ;

	smc->os.smc_version = SMC_VERSION ;
	smt_boot_time = smt_get_time();
	for( i = 0; i < NUMMACS; i++ )
		smc->sm.last_tok_time[i] = smt_boot_time ;
	smt = &smc->s ;
	smt->attach_s = 0 ;
	smt->build_ring_map = 1 ;
	smt->sas = SMT_DAS ;
	smt->numphys = NUMPHYS ;
	smt->pcm_tb_min = DEFAULT_TB_MIN ;
	smt->pcm_tb_max = DEFAULT_TB_MAX ;
	smt->pcm_c_min = DEFAULT_C_MIN ;
	smt->pcm_t_out = DEFAULT_T_OUT ;
	smt->pcm_tl_min = DEFAULT_TL_MIN ;
	smt->pcm_lc_short = DEFAULT_LC_SHORT ;
	smt->pcm_lc_medium = DEFAULT_LC_MEDIUM ;
	smt->pcm_lc_long = DEFAULT_LC_LONG ;
	smt->pcm_lc_extended = DEFAULT_LC_EXTENDED ;
	smt->pcm_t_next_9 = DEFAULT_T_NEXT_9 ;
	smt->pcm_ns_max = DEFAULT_NS_MAX ;
	smt->ecm_i_max = DEFAULT_I_MAX ;
	smt->ecm_in_max = DEFAULT_IN_MAX ;
	smt->ecm_td_min = DEFAULT_TD_MIN ;
	smt->ecm_test_done = DEFAULT_TEST_DONE ;
	smt->ecm_check_poll = DEFAULT_CHECK_POLL ;
	smt->rmt_t_non_op = DEFAULT_T_NON_OP ;
	smt->rmt_t_stuck = DEFAULT_T_STUCK ;
	smt->rmt_t_direct = DEFAULT_T_DIRECT ;
	smt->rmt_t_jam = DEFAULT_T_JAM ;
	smt->rmt_t_announce = DEFAULT_T_ANNOUNCE ;
	smt->rmt_t_poll = DEFAULT_POLL ;
        smt->rmt_dup_mac_behavior = FALSE ;  /* See Struct smt_config */
	smt->mac_d_max = DEFAULT_D_MAX ;

	smt->lct_short = DEFAULT_LCT_SHORT ;
	smt->lct_medium = DEFAULT_LCT_MEDIUM ;
	smt->lct_long = DEFAULT_LCT_LONG ;
	smt->lct_extended = DEFAULT_LCT_EXTEND ;

#ifndef	SLIM_SMT
#ifdef	ESS
	if (level == 0) {
		smc->ess.sync_bw_available = FALSE ;
		smc->mib.fddiESSPayload = 0 ;
		smc->mib.fddiESSOverhead = 0 ;
		smc->mib.fddiESSMaxTNeg = (u_long)(- MS2BCLK(25)) ;
		smc->mib.fddiESSMinSegmentSize = 1 ;
		smc->mib.fddiESSCategory = SB_STATIC ;
		smc->mib.fddiESSSynchTxMode = FALSE ;
		smc->ess.raf_act_timer_poll = FALSE ;
		smc->ess.timer_count = 7 ; 	/* first RAF alc req after 3s */
	}
	smc->ess.local_sba_active = FALSE ;
	smc->ess.sba_reply_pend = NULL ;
#endif
#ifdef	SBA
	smt_init_sba(smc,level) ;
#endif
#endif	/* no SLIM_SMT */
#ifdef	TAG_MODE
	if (level == 0) {
		smc->hw.pci_fix_value = 0 ;
	}
#endif
}

/*
 * manufacturer data
 */
static const char man_data[32] =
/*	 01234567890123456789012345678901	*/
	"xxxSK-NET FDDI SMT 7.3 - V2.8.8" ;

static void smt_init_mib(struct s_smc *smc, int level)
{
	struct fddi_mib		*mib ;
	struct fddi_mib_p	*pm ;
	int			port ;
	int			path ;

	mib = &smc->mib ;
	if (level == 0) {
		/*
		 * set EVERYTHING to ZERO
		 * EXCEPT hw and os
		 */
		memset(((char *)smc)+
			sizeof(struct s_smt_os)+sizeof(struct s_smt_hw), 0,
			sizeof(struct s_smc) -
			sizeof(struct s_smt_os) - sizeof(struct s_smt_hw)) ;
	}
	else {
		mib->fddiSMTRemoteDisconnectFlag = 0 ;
		mib->fddiSMTPeerWrapFlag = 0 ;
	}

	mib->fddiSMTOpVersionId = 2 ;
	mib->fddiSMTHiVersionId = 2 ;
	mib->fddiSMTLoVersionId = 2 ;
	memcpy((char *) mib->fddiSMTManufacturerData,man_data,32) ;
	if (level == 0) {
		strcpy(mib->fddiSMTUserData,OEM_USER_DATA) ;
	}
	mib->fddiSMTMIBVersionId = 1 ;
	mib->fddiSMTMac_Ct = NUMMACS ;
	mib->fddiSMTConnectionPolicy = POLICY_MM | POLICY_AA | POLICY_BB ;

	/*
	 * fddiSMTNonMaster_Ct and fddiSMTMaster_Ct are set in smt_fixup_mib
	 * s.sas is not set yet (is set in init driver)
	 */
	mib->fddiSMTAvailablePaths = MIB_PATH_P | MIB_PATH_S ;

	mib->fddiSMTConfigCapabilities = 0 ;	/* no hold,no wrap_ab*/
	mib->fddiSMTTT_Notify = 10 ;
	mib->fddiSMTStatRptPolicy = TRUE ;
	mib->fddiSMTTrace_MaxExpiration = SEC2MIB(7) ;
	mib->fddiSMTMACIndexes = INDEX_MAC ;
	mib->fddiSMTStationStatus = MIB_SMT_STASTA_SEPA ;	/* separated */

	mib->m[MAC0].fddiMACIndex = INDEX_MAC ;
	mib->m[MAC0].fddiMACFrameStatusFunctions = FSC_TYPE0 ;
	mib->m[MAC0].fddiMACRequestedPaths =
		MIB_P_PATH_LOCAL |
		MIB_P_PATH_SEC_ALTER |
		MIB_P_PATH_PRIM_ALTER ;
	mib->m[MAC0].fddiMACAvailablePaths = MIB_PATH_P ;
	mib->m[MAC0].fddiMACCurrentPath = MIB_PATH_PRIMARY ;
	mib->m[MAC0].fddiMACT_MaxCapabilitiy = (u_long)(- MS2BCLK(165)) ;
	mib->m[MAC0].fddiMACTVXCapabilitiy = (u_long)(- US2BCLK(52)) ;
	if (level == 0) {
		mib->m[MAC0].fddiMACTvxValue = (u_long)(- US2BCLK(27)) ;
		mib->m[MAC0].fddiMACTvxValueMIB = (u_long)(- US2BCLK(27)) ;
		mib->m[MAC0].fddiMACT_Req = (u_long)(- MS2BCLK(165)) ;
		mib->m[MAC0].fddiMACT_ReqMIB = (u_long)(- MS2BCLK(165)) ;
		mib->m[MAC0].fddiMACT_Max = (u_long)(- MS2BCLK(165)) ;
		mib->m[MAC0].fddiMACT_MaxMIB = (u_long)(- MS2BCLK(165)) ;
		mib->m[MAC0].fddiMACT_Min = (u_long)(- MS2BCLK(4)) ;
	}
	mib->m[MAC0].fddiMACHardwarePresent = TRUE ;
	mib->m[MAC0].fddiMACMA_UnitdataEnable = TRUE ;
	mib->m[MAC0].fddiMACFrameErrorThreshold = 1 ;
	mib->m[MAC0].fddiMACNotCopiedThreshold = 1 ;
	/*
	 * Path attributes
	 */
	for (path = 0 ; path < NUMPATHS ; path++) {
		mib->a[path].fddiPATHIndex = INDEX_PATH + path ;
		if (level == 0) {
			mib->a[path].fddiPATHTVXLowerBound =
				(u_long)(- US2BCLK(27)) ;
			mib->a[path].fddiPATHT_MaxLowerBound =
				(u_long)(- MS2BCLK(165)) ;
			mib->a[path].fddiPATHMaxT_Req =
				(u_long)(- MS2BCLK(165)) ;
		}
	}


	/*
	 * Port attributes
	 */
	pm = mib->p ;
	for (port = 0 ; port <  NUMPHYS ; port++) {
		/*
		 * set MIB pointer in phy
		 */
		/* Attention: don't initialize mib pointer here! */
		/*  It must be initialized during phase 2 */
		smc->y[port].mib = NULL;
		mib->fddiSMTPORTIndexes[port] = port+INDEX_PORT ;

		pm->fddiPORTIndex = port+INDEX_PORT ;
		pm->fddiPORTHardwarePresent = TRUE ;
		if (level == 0) {
			pm->fddiPORTLer_Alarm = DEFAULT_LEM_ALARM ;
			pm->fddiPORTLer_Cutoff = DEFAULT_LEM_CUTOFF ;
		}
		/*
		 * fddiPORTRequestedPaths are set in pcmplc.c
		 * we don't know the port type yet !
		 */
		pm->fddiPORTRequestedPaths[1] = 0 ;
		pm->fddiPORTRequestedPaths[2] = 0 ;
		pm->fddiPORTRequestedPaths[3] = 0 ;
		pm->fddiPORTAvailablePaths = MIB_PATH_P ;
		pm->fddiPORTPMDClass = MIB_PMDCLASS_MULTI ;
		pm++ ;
	}

	(void) smt_set_mac_opvalues(smc) ;
}

int smt_set_mac_opvalues(struct s_smc *smc)
{
	int	st ;
	int	st2 ;

	st = set_min_max(1,smc->mib.m[MAC0].fddiMACTvxValueMIB,
		smc->mib.a[PATH0].fddiPATHTVXLowerBound,
		&smc->mib.m[MAC0].fddiMACTvxValue) ;
	st |= set_min_max(0,smc->mib.m[MAC0].fddiMACT_MaxMIB,
		smc->mib.a[PATH0].fddiPATHT_MaxLowerBound,
		&smc->mib.m[MAC0].fddiMACT_Max) ;
	st |= (st2 = set_min_max(0,smc->mib.m[MAC0].fddiMACT_ReqMIB,
		smc->mib.a[PATH0].fddiPATHMaxT_Req,
		&smc->mib.m[MAC0].fddiMACT_Req)) ;
	if (st2) {
		/* Treq attribute changed remotely. So send an AIX_EVENT to the
		 * user
		 */
		AIX_EVENT(smc, (u_long) FDDI_RING_STATUS, (u_long)
			FDDI_SMT_EVENT, (u_long) FDDI_REMOTE_T_REQ,
			smt_get_event_word(smc));
	}
	return(st) ;
}

void smt_fixup_mib(struct s_smc *smc)
{
#ifdef	CONCENTRATOR
	switch (smc->s.sas) {
	case SMT_SAS :
		smc->mib.fddiSMTNonMaster_Ct = 1 ;
		break ;
	case SMT_DAS :
		smc->mib.fddiSMTNonMaster_Ct = 2 ;
		break ;
	case SMT_NAC :
		smc->mib.fddiSMTNonMaster_Ct = 0 ;
		break ;
	}
	smc->mib.fddiSMTMaster_Ct = NUMPHYS - smc->mib.fddiSMTNonMaster_Ct ;
#else
	switch (smc->s.sas) {
	case SMT_SAS :
		smc->mib.fddiSMTNonMaster_Ct = 1 ;
		break ;
	case SMT_DAS :
		smc->mib.fddiSMTNonMaster_Ct = 2 ;
		break ;
	}
	smc->mib.fddiSMTMaster_Ct = 0 ;
#endif
}

/*
 * determine new setting for operational value
 * if limit is lower than mib
 *	use limit
 * else
 *	use mib
 * NOTE : numbers are negative, negate comparison !
 */
static int set_min_max(int maxflag, u_long mib, u_long limit, u_long *oper)
{
	u_long	old ;
	old = *oper ;
	if ((limit > mib) ^ maxflag)
		*oper = limit ;
	else
		*oper = mib ;
	return(old != *oper) ;
}

