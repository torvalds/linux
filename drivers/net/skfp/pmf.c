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
	Parameter Management Frame processing for SMT 7.2
*/

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"
#include "h/smt_p.h"

#define KERNEL
#include "h/smtstate.h"

#ifndef	SLIM_SMT

#ifndef	lint
static const char ID_sccs[] = "@(#)pmf.c	1.37 97/08/04 (C) SK " ;
#endif

static int smt_authorize(struct s_smc *smc, struct smt_header *sm);
static int smt_check_set_count(struct s_smc *smc, struct smt_header *sm);
static const struct s_p_tab* smt_get_ptab(u_short para);
static int smt_mib_phys(struct s_smc *smc);
static int smt_set_para(struct s_smc *smc, struct smt_para *pa, int index,
			int local, int set);
void smt_add_para(struct s_smc *smc, struct s_pcon *pcon, u_short para,
		  int index, int local);
static SMbuf *smt_build_pmf_response(struct s_smc *smc, struct smt_header *req,
				     int set, int local);
static int port_to_mib(struct s_smc *smc, int p);

#define MOFFSS(e)	((int)&(((struct fddi_mib *)0)->e))
#define MOFFSA(e)	((int) (((struct fddi_mib *)0)->e))

#define MOFFMS(e)	((int)&(((struct fddi_mib_m *)0)->e))
#define MOFFMA(e)	((int) (((struct fddi_mib_m *)0)->e))

#define MOFFAS(e)	((int)&(((struct fddi_mib_a *)0)->e))
#define MOFFAA(e)	((int) (((struct fddi_mib_a *)0)->e))

#define MOFFPS(e)	((int)&(((struct fddi_mib_p *)0)->e))
#define MOFFPA(e)	((int) (((struct fddi_mib_p *)0)->e))


#define AC_G	0x01		/* Get */
#define AC_GR	0x02		/* Get/Set */
#define AC_S	0x04		/* Set */
#define AC_NA	0x08
#define AC_GROUP	0x10		/* Group */
#define MS2BCLK(x)	((x)*12500L)
/*
	F	LFag (byte)
	B	byte
	S	u_short	16 bit
	C	Counter 32 bit
	L	Long 32 bit
	T	Timer_2	32 bit
	P	TimeStamp ;
	A	LongAddress (6 byte)
	E	Enum 16 bit
	R	ResId 16 Bit
*/
static const struct s_p_tab {
	u_short	p_num ;		/* parameter code */
	u_char	p_access ;	/* access rights */
	u_short	p_offset ;	/* offset in mib */
	char	p_swap[3] ;	/* format string */
} p_tab[] = {
	/* StationIdGrp */
	{ SMT_P100A,AC_GROUP	} ,
	{ SMT_P100B,AC_G,	MOFFSS(fddiSMTStationId),	"8"	} ,
	{ SMT_P100D,AC_G,	MOFFSS(fddiSMTOpVersionId),	"S"	} ,
	{ SMT_P100E,AC_G,	MOFFSS(fddiSMTHiVersionId),	"S"	} ,
	{ SMT_P100F,AC_G,	MOFFSS(fddiSMTLoVersionId),	"S"	} ,
	{ SMT_P1010,AC_G,	MOFFSA(fddiSMTManufacturerData), "D" } ,
	{ SMT_P1011,AC_GR,	MOFFSA(fddiSMTUserData),	"D"	} ,
	{ SMT_P1012,AC_G,	MOFFSS(fddiSMTMIBVersionId),	"S"	} ,

	/* StationConfigGrp */
	{ SMT_P1014,AC_GROUP	} ,
	{ SMT_P1015,AC_G,	MOFFSS(fddiSMTMac_Ct),		"B"	} ,
	{ SMT_P1016,AC_G,	MOFFSS(fddiSMTNonMaster_Ct),	"B"	} ,
	{ SMT_P1017,AC_G,	MOFFSS(fddiSMTMaster_Ct),	"B"	} ,
	{ SMT_P1018,AC_G,	MOFFSS(fddiSMTAvailablePaths),	"B"	} ,
	{ SMT_P1019,AC_G,	MOFFSS(fddiSMTConfigCapabilities),"S"	} ,
	{ SMT_P101A,AC_GR,	MOFFSS(fddiSMTConfigPolicy),	"wS"	} ,
	{ SMT_P101B,AC_GR,	MOFFSS(fddiSMTConnectionPolicy),"wS"	} ,
	{ SMT_P101D,AC_GR,	MOFFSS(fddiSMTTT_Notify),	"wS"	} ,
	{ SMT_P101E,AC_GR,	MOFFSS(fddiSMTStatRptPolicy),	"bB"	} ,
	{ SMT_P101F,AC_GR,	MOFFSS(fddiSMTTrace_MaxExpiration),"lL"	} ,
	{ SMT_P1020,AC_G,	MOFFSA(fddiSMTPORTIndexes),	"II"	} ,
	{ SMT_P1021,AC_G,	MOFFSS(fddiSMTMACIndexes),	"I"	} ,
	{ SMT_P1022,AC_G,	MOFFSS(fddiSMTBypassPresent),	"F"	} ,

	/* StatusGrp */
	{ SMT_P1028,AC_GROUP	} ,
	{ SMT_P1029,AC_G,	MOFFSS(fddiSMTECMState),	"E"	} ,
	{ SMT_P102A,AC_G,	MOFFSS(fddiSMTCF_State),	"E"	} ,
	{ SMT_P102C,AC_G,	MOFFSS(fddiSMTRemoteDisconnectFlag),"F"	} ,
	{ SMT_P102D,AC_G,	MOFFSS(fddiSMTStationStatus),	"E"	} ,
	{ SMT_P102E,AC_G,	MOFFSS(fddiSMTPeerWrapFlag),	"F"	} ,

	/* MIBOperationGrp */
	{ SMT_P1032,AC_GROUP	} ,
	{ SMT_P1033,AC_G,	MOFFSA(fddiSMTTimeStamp),"P"		} ,
	{ SMT_P1034,AC_G,	MOFFSA(fddiSMTTransitionTimeStamp),"P"	} ,
	/* NOTE : SMT_P1035 is already swapped ! SMT_P_SETCOUNT */
	{ SMT_P1035,AC_G,	MOFFSS(fddiSMTSetCount),"4P"		} ,
	{ SMT_P1036,AC_G,	MOFFSS(fddiSMTLastSetStationId),"8"	} ,

	{ SMT_P103C,AC_S,	0,				"wS"	} ,

	/*
	 * PRIVATE EXTENSIONS
	 * only accessible locally to get/set passwd
	 */
	{ SMT_P10F0,AC_GR,	MOFFSA(fddiPRPMFPasswd),	"8"	} ,
	{ SMT_P10F1,AC_GR,	MOFFSS(fddiPRPMFStation),	"8"	} ,
#ifdef	ESS
	{ SMT_P10F2,AC_GR,	MOFFSS(fddiESSPayload),		"lL"	} ,
	{ SMT_P10F3,AC_GR,	MOFFSS(fddiESSOverhead),	"lL"	} ,
	{ SMT_P10F4,AC_GR,	MOFFSS(fddiESSMaxTNeg),		"lL"	} ,
	{ SMT_P10F5,AC_GR,	MOFFSS(fddiESSMinSegmentSize),	"lL"	} ,
	{ SMT_P10F6,AC_GR,	MOFFSS(fddiESSCategory),	"lL"	} ,
	{ SMT_P10F7,AC_GR,	MOFFSS(fddiESSSynchTxMode),	"wS"	} ,
#endif
#ifdef	SBA
	{ SMT_P10F8,AC_GR,	MOFFSS(fddiSBACommand),		"bF"	} ,
	{ SMT_P10F9,AC_GR,	MOFFSS(fddiSBAAvailable),	"bF"	} ,
#endif
	/* MAC Attributes */
	{ SMT_P200A,AC_GROUP	} ,
	{ SMT_P200B,AC_G,	MOFFMS(fddiMACFrameStatusFunctions),"S"	} ,
	{ SMT_P200D,AC_G,	MOFFMS(fddiMACT_MaxCapabilitiy),"T"	} ,
	{ SMT_P200E,AC_G,	MOFFMS(fddiMACTVXCapabilitiy),"T"	} ,

	/* ConfigGrp */
	{ SMT_P2014,AC_GROUP	} ,
	{ SMT_P2016,AC_G,	MOFFMS(fddiMACAvailablePaths),	"B"	} ,
	{ SMT_P2017,AC_G,	MOFFMS(fddiMACCurrentPath),	"S"	} ,
	{ SMT_P2018,AC_G,	MOFFMS(fddiMACUpstreamNbr),	"A"	} ,
	{ SMT_P2019,AC_G,	MOFFMS(fddiMACDownstreamNbr),	"A"	} ,
	{ SMT_P201A,AC_G,	MOFFMS(fddiMACOldUpstreamNbr),	"A"	} ,
	{ SMT_P201B,AC_G,	MOFFMS(fddiMACOldDownstreamNbr),"A"	} ,
	{ SMT_P201D,AC_G,	MOFFMS(fddiMACDupAddressTest),	"E"	} ,
	{ SMT_P2020,AC_GR,	MOFFMS(fddiMACRequestedPaths),	"wS"	} ,
	{ SMT_P2021,AC_G,	MOFFMS(fddiMACDownstreamPORTType),"E"	} ,
	{ SMT_P2022,AC_G,	MOFFMS(fddiMACIndex),		"S"	} ,

	/* AddressGrp */
	{ SMT_P2028,AC_GROUP	} ,
	{ SMT_P2029,AC_G,	MOFFMS(fddiMACSMTAddress),	"A"	} ,

	/* OperationGrp */
	{ SMT_P2032,AC_GROUP	} ,
	{ SMT_P2033,AC_G,	MOFFMS(fddiMACT_Req),		"T"	} ,
	{ SMT_P2034,AC_G,	MOFFMS(fddiMACT_Neg),		"T"	} ,
	{ SMT_P2035,AC_G,	MOFFMS(fddiMACT_Max),		"T"	} ,
	{ SMT_P2036,AC_G,	MOFFMS(fddiMACTvxValue),	"T"	} ,
	{ SMT_P2038,AC_G,	MOFFMS(fddiMACT_Pri0),		"T"	} ,
	{ SMT_P2039,AC_G,	MOFFMS(fddiMACT_Pri1),		"T"	} ,
	{ SMT_P203A,AC_G,	MOFFMS(fddiMACT_Pri2),		"T"	} ,
	{ SMT_P203B,AC_G,	MOFFMS(fddiMACT_Pri3),		"T"	} ,
	{ SMT_P203C,AC_G,	MOFFMS(fddiMACT_Pri4),		"T"	} ,
	{ SMT_P203D,AC_G,	MOFFMS(fddiMACT_Pri5),		"T"	} ,
	{ SMT_P203E,AC_G,	MOFFMS(fddiMACT_Pri6),		"T"	} ,


	/* CountersGrp */
	{ SMT_P2046,AC_GROUP	} ,
	{ SMT_P2047,AC_G,	MOFFMS(fddiMACFrame_Ct),	"C"	} ,
	{ SMT_P2048,AC_G,	MOFFMS(fddiMACCopied_Ct),	"C"	} ,
	{ SMT_P2049,AC_G,	MOFFMS(fddiMACTransmit_Ct),	"C"	} ,
	{ SMT_P204A,AC_G,	MOFFMS(fddiMACToken_Ct),	"C"	} ,
	{ SMT_P2051,AC_G,	MOFFMS(fddiMACError_Ct),	"C"	} ,
	{ SMT_P2052,AC_G,	MOFFMS(fddiMACLost_Ct),		"C"	} ,
	{ SMT_P2053,AC_G,	MOFFMS(fddiMACTvxExpired_Ct),	"C"	} ,
	{ SMT_P2054,AC_G,	MOFFMS(fddiMACNotCopied_Ct),	"C"	} ,
	{ SMT_P2056,AC_G,	MOFFMS(fddiMACRingOp_Ct),	"C"	} ,

	/* FrameErrorConditionGrp */
	{ SMT_P205A,AC_GROUP	} ,
	{ SMT_P205F,AC_GR,	MOFFMS(fddiMACFrameErrorThreshold),"wS"	} ,
	{ SMT_P2060,AC_G,	MOFFMS(fddiMACFrameErrorRatio),	"S"	} ,

	/* NotCopiedConditionGrp */
	{ SMT_P2064,AC_GROUP	} ,
	{ SMT_P2067,AC_GR,	MOFFMS(fddiMACNotCopiedThreshold),"wS"	} ,
	{ SMT_P2069,AC_G,	MOFFMS(fddiMACNotCopiedRatio),	"S"	} ,

	/* StatusGrp */
	{ SMT_P206E,AC_GROUP	} ,
	{ SMT_P206F,AC_G,	MOFFMS(fddiMACRMTState),	"S"	} ,
	{ SMT_P2070,AC_G,	MOFFMS(fddiMACDA_Flag),	"F"	} ,
	{ SMT_P2071,AC_G,	MOFFMS(fddiMACUNDA_Flag),	"F"	} ,
	{ SMT_P2072,AC_G,	MOFFMS(fddiMACFrameErrorFlag),	"F"	} ,
	{ SMT_P2073,AC_G,	MOFFMS(fddiMACNotCopiedFlag),	"F"	} ,
	{ SMT_P2074,AC_G,	MOFFMS(fddiMACMA_UnitdataAvailable),"F"	} ,
	{ SMT_P2075,AC_G,	MOFFMS(fddiMACHardwarePresent),	"F"	} ,
	{ SMT_P2076,AC_GR,	MOFFMS(fddiMACMA_UnitdataEnable),"bF"	} ,

	/*
	 * PRIVATE EXTENSIONS
	 * only accessible locally to get/set TMIN
	 */
	{ SMT_P20F0,AC_NA						} ,
	{ SMT_P20F1,AC_GR,	MOFFMS(fddiMACT_Min),		"lT"	} ,

	/* Path Attributes */
	/*
	 * DON't swap 320B,320F,3210: they are already swapped in swap_para()
	 */
	{ SMT_P320A,AC_GROUP	} ,
	{ SMT_P320B,AC_G,	MOFFAS(fddiPATHIndex),		"r"	} ,
	{ SMT_P320F,AC_GR,	MOFFAS(fddiPATHSbaPayload),	"l4"	} ,
	{ SMT_P3210,AC_GR,	MOFFAS(fddiPATHSbaOverhead),	"l4"	} ,
	/* fddiPATHConfiguration */
	{ SMT_P3212,AC_G,	0,				""	} ,
	{ SMT_P3213,AC_GR,	MOFFAS(fddiPATHT_Rmode),	"lT"	} ,
	{ SMT_P3214,AC_GR,	MOFFAS(fddiPATHSbaAvailable),	"lL"	} ,
	{ SMT_P3215,AC_GR,	MOFFAS(fddiPATHTVXLowerBound),	"lT"	} ,
	{ SMT_P3216,AC_GR,	MOFFAS(fddiPATHT_MaxLowerBound),"lT"	} ,
	{ SMT_P3217,AC_GR,	MOFFAS(fddiPATHMaxT_Req),	"lT"	} ,

	/* Port Attributes */
	/* ConfigGrp */
	{ SMT_P400A,AC_GROUP	} ,
	{ SMT_P400C,AC_G,	MOFFPS(fddiPORTMy_Type),	"E"	} ,
	{ SMT_P400D,AC_G,	MOFFPS(fddiPORTNeighborType),	"E"	} ,
	{ SMT_P400E,AC_GR,	MOFFPS(fddiPORTConnectionPolicies),"bB"	} ,
	{ SMT_P400F,AC_G,	MOFFPS(fddiPORTMacIndicated),	"2"	} ,
	{ SMT_P4010,AC_G,	MOFFPS(fddiPORTCurrentPath),	"E"	} ,
	{ SMT_P4011,AC_GR,	MOFFPA(fddiPORTRequestedPaths),	"l4"	} ,
	{ SMT_P4012,AC_G,	MOFFPS(fddiPORTMACPlacement),	"S"	} ,
	{ SMT_P4013,AC_G,	MOFFPS(fddiPORTAvailablePaths),	"B"	} ,
	{ SMT_P4016,AC_G,	MOFFPS(fddiPORTPMDClass),	"E"	} ,
	{ SMT_P4017,AC_G,	MOFFPS(fddiPORTConnectionCapabilities),	"B"} ,
	{ SMT_P401D,AC_G,	MOFFPS(fddiPORTIndex),		"R"	} ,

	/* OperationGrp */
	{ SMT_P401E,AC_GROUP	} ,
	{ SMT_P401F,AC_GR,	MOFFPS(fddiPORTMaint_LS),	"wE"	} ,
	{ SMT_P4021,AC_G,	MOFFPS(fddiPORTBS_Flag),	"F"	} ,
	{ SMT_P4022,AC_G,	MOFFPS(fddiPORTPC_LS),		"E"	} ,

	/* ErrorCtrsGrp */
	{ SMT_P4028,AC_GROUP	} ,
	{ SMT_P4029,AC_G,	MOFFPS(fddiPORTEBError_Ct),	"C"	} ,
	{ SMT_P402A,AC_G,	MOFFPS(fddiPORTLCTFail_Ct),	"C"	} ,

	/* LerGrp */
	{ SMT_P4032,AC_GROUP	} ,
	{ SMT_P4033,AC_G,	MOFFPS(fddiPORTLer_Estimate),	"F"	} ,
	{ SMT_P4034,AC_G,	MOFFPS(fddiPORTLem_Reject_Ct),	"C"	} ,
	{ SMT_P4035,AC_G,	MOFFPS(fddiPORTLem_Ct),		"C"	} ,
	{ SMT_P403A,AC_GR,	MOFFPS(fddiPORTLer_Cutoff),	"bB"	} ,
	{ SMT_P403B,AC_GR,	MOFFPS(fddiPORTLer_Alarm),	"bB"	} ,

	/* StatusGrp */
	{ SMT_P403C,AC_GROUP	} ,
	{ SMT_P403D,AC_G,	MOFFPS(fddiPORTConnectState),	"E"	} ,
	{ SMT_P403E,AC_G,	MOFFPS(fddiPORTPCMStateX),	"E"	} ,
	{ SMT_P403F,AC_G,	MOFFPS(fddiPORTPC_Withhold),	"E"	} ,
	{ SMT_P4040,AC_G,	MOFFPS(fddiPORTLerFlag),	"F"	} ,
	{ SMT_P4041,AC_G,	MOFFPS(fddiPORTHardwarePresent),"F"	} ,

	{ SMT_P4046,AC_S,	0,				"wS"	} ,

	{ 0,	AC_GROUP	} ,
	{ 0 }
} ;

void smt_pmf_received_pack(struct s_smc *smc, SMbuf *mb, int local)
{
	struct smt_header	*sm ;
	SMbuf		*reply ;

	sm = smtod(mb,struct smt_header *) ;
	DB_SMT("SMT: processing PMF frame at %x len %d\n",sm,mb->sm_len) ;
#ifdef	DEBUG
	dump_smt(smc,sm,"PMF Received") ;
#endif
	/*
	 * Start the watchdog: It may be a long, long packet and
	 * maybe the watchdog occurs ...
	 */
	smt_start_watchdog(smc) ;

	if (sm->smt_class == SMT_PMF_GET ||
	    sm->smt_class == SMT_PMF_SET) {
		reply = smt_build_pmf_response(smc,sm,
			sm->smt_class == SMT_PMF_SET,local) ;
		if (reply) {
			sm = smtod(reply,struct smt_header *) ;
#ifdef	DEBUG
			dump_smt(smc,sm,"PMF Reply") ;
#endif
			smt_send_frame(smc,reply,FC_SMT_INFO,local) ;
		}
	}
}

static SMbuf *smt_build_pmf_response(struct s_smc *smc, struct smt_header *req,
				     int set, int local)
{
	SMbuf			*mb ;
	struct smt_header	*smt ;
	struct smt_para		*pa ;
	struct smt_p_reason	*res ;
	const struct s_p_tab	*pt ;
	int			len ;
	int			index ;
	int			idx_end ;
	int			error ;
	int			range ;
	SK_LOC_DECL(struct s_pcon,pcon) ;
	SK_LOC_DECL(struct s_pcon,set_pcon) ;

	/*
	 * build SMT header
	 */
	if (!(mb = smt_get_mbuf(smc)))
		return(mb) ;

	smt = smtod(mb, struct smt_header *) ;
	smt->smt_dest = req->smt_source ;	/* DA == source of request */
	smt->smt_class = req->smt_class ;	/* same class (GET/SET) */
	smt->smt_type = SMT_REPLY ;
	smt->smt_version = SMT_VID_2 ;
	smt->smt_tid = req->smt_tid ;		/* same TID */
	smt->smt_pad = 0 ;
	smt->smt_len = 0 ;

	/*
	 * setup parameter status
	 */
	pcon.pc_len = SMT_MAX_INFO_LEN ;	/* max para length */
	pcon.pc_err = 0 ;			/* no error */
	pcon.pc_badset = 0 ;			/* no bad set count */
	pcon.pc_p = (void *) (smt + 1) ;	/* paras start here */

	/*
	 * check authoriziation and set count
	 */
	error = 0 ;
	if (set) {
		if (!local && smt_authorize(smc,req))
			error = SMT_RDF_AUTHOR ;
		else if (smt_check_set_count(smc,req))
			pcon.pc_badset = SMT_RDF_BADSET ;
	}
	/*
	 * add reason code and all mandatory parameters
	 */
	res = (struct smt_p_reason *) pcon.pc_p ;
	smt_add_para(smc,&pcon,(u_short) SMT_P_REASON,0,0) ;
	smt_add_para(smc,&pcon,(u_short) SMT_P1033,0,0) ;
	/* update 1035 and 1036 later if set */
	set_pcon = pcon ;
	smt_add_para(smc,&pcon,(u_short) SMT_P1035,0,0) ;
	smt_add_para(smc,&pcon,(u_short) SMT_P1036,0,0) ;

	pcon.pc_err = error ;
	len = req->smt_len ;
	pa = (struct smt_para *) (req + 1) ;
	/*
	 * process list of paras
	 */
	while (!pcon.pc_err && len > 0 ) {
		if (((u_short)len < pa->p_len + PARA_LEN) || (pa->p_len & 3)) {
			pcon.pc_err = SMT_RDF_LENGTH ;
			break ;
		}

		if (((range = (pa->p_type & 0xf000)) == 0x2000) ||
			range == 0x3000 || range == 0x4000) {
			/*
			 * get index for PART,MAC ad PATH group
			 */
			index = *((u_char *)pa + PARA_LEN + 3) ;/* index */
			idx_end = index ;
			if (!set && (pa->p_len != 4)) {
				pcon.pc_err = SMT_RDF_LENGTH ;
				break ;
			}
			if (!index && !set) {
				switch (range) {
				case 0x2000 :
					index = INDEX_MAC ;
					idx_end = index - 1 + NUMMACS ;
					break ;
				case 0x3000 :
					index = INDEX_PATH ;
					idx_end = index - 1 + NUMPATHS ;
					break ;
				case 0x4000 :
					index = INDEX_PORT ;
					idx_end = index - 1 + NUMPHYS ;
#ifndef	CONCENTRATOR
					if (smc->s.sas == SMT_SAS)
						idx_end = INDEX_PORT ;
#endif
					break ;
				}
			}
		}
		else {
			/*
			 * smt group has no index
			 */
			if (!set && (pa->p_len != 0)) {
				pcon.pc_err = SMT_RDF_LENGTH ;
				break ;
			}
			index = 0 ;
			idx_end = 0 ;
		}
		while (index <= idx_end) {
			/*
			 * if group
			 *	add all paras of group
			 */
			pt = smt_get_ptab(pa->p_type) ;
			if (pt && pt->p_access == AC_GROUP && !set) {
				pt++ ;
				while (pt->p_access == AC_G ||
					pt->p_access == AC_GR) {
					smt_add_para(smc,&pcon,pt->p_num,
						index,local);
					pt++ ;
				}
			}
			/*
			 * ignore
			 *	AUTHORIZATION in get/set
			 *	SET COUNT in set
			 */
			else if (pa->p_type != SMT_P_AUTHOR &&
				 (!set || (pa->p_type != SMT_P1035))) {
				int	st ;
				if (pcon.pc_badset) {
					smt_add_para(smc,&pcon,pa->p_type,
						index,local) ;
				}
				else if (set) {
					st = smt_set_para(smc,pa,index,local,1);
					/*
					 * return para even if error
					 */
					smt_add_para(smc,&pcon,pa->p_type,
						index,local) ;
					pcon.pc_err = st ;
				}
				else {
					if (pt && pt->p_access == AC_S) {
						pcon.pc_err =
							SMT_RDF_ILLEGAL ;
					}
					smt_add_para(smc,&pcon,pa->p_type,
						index,local) ;
				}
			}
			if (pcon.pc_err)
				break ;
			index++ ;
		}
		len -= pa->p_len + PARA_LEN ;
		pa = (struct smt_para *) ((char *)pa + pa->p_len + PARA_LEN) ;
	}
	smt->smt_len = SMT_MAX_INFO_LEN - pcon.pc_len ;
	mb->sm_len = smt->smt_len + sizeof(struct smt_header) ;

	/* update reason code */
	res->rdf_reason = pcon.pc_badset ? pcon.pc_badset :
			pcon.pc_err ? pcon.pc_err : SMT_RDF_SUCCESS ;
	if (set && (res->rdf_reason == SMT_RDF_SUCCESS)) {
		/*
		 * increment set count
		 * set time stamp
		 * store station id of last set
		 */
		smc->mib.fddiSMTSetCount.count++ ;
		smt_set_timestamp(smc,smc->mib.fddiSMTSetCount.timestamp) ;
		smc->mib.fddiSMTLastSetStationId = req->smt_sid ;
		smt_add_para(smc,&set_pcon,(u_short) SMT_P1035,0,0) ;
		smt_add_para(smc,&set_pcon,(u_short) SMT_P1036,0,0) ;
	}
	return(mb) ;
}

static int smt_authorize(struct s_smc *smc, struct smt_header *sm)
{
	struct smt_para	*pa ;
	int		i ;
	char		*p ;

	/*
	 * check source station id if not zero
	 */
	p = (char *) &smc->mib.fddiPRPMFStation ;
	for (i = 0 ; i < 8 && !p[i] ; i++)
		;
	if (i != 8) {
		if (memcmp((char *) &sm->smt_sid,
			(char *) &smc->mib.fddiPRPMFStation,8))
			return(1) ;
	}
	/*
	 * check authoriziation parameter if passwd not zero
	 */
	p = (char *) smc->mib.fddiPRPMFPasswd ;
	for (i = 0 ; i < 8 && !p[i] ; i++)
		;
	if (i != 8) {
		pa = (struct smt_para *) sm_to_para(smc,sm,SMT_P_AUTHOR) ;
		if (!pa)
			return(1) ;
		if (pa->p_len != 8)
			return(1) ;
		if (memcmp((char *)(pa+1),(char *)smc->mib.fddiPRPMFPasswd,8))
			return(1) ;
	}
	return(0) ;
}

static int smt_check_set_count(struct s_smc *smc, struct smt_header *sm)
{
	struct smt_para	*pa ;
	struct smt_p_setcount	*sc ;

	pa = (struct smt_para *) sm_to_para(smc,sm,SMT_P1035) ;
	if (pa) {
		sc = (struct smt_p_setcount *) pa ;
		if ((smc->mib.fddiSMTSetCount.count != sc->count) ||
			memcmp((char *) smc->mib.fddiSMTSetCount.timestamp,
			(char *)sc->timestamp,8))
			return(1) ;
	}
	return(0) ;
}

void smt_add_para(struct s_smc *smc, struct s_pcon *pcon, u_short para,
		  int index, int local)
{
	struct smt_para	*pa ;
	const struct s_p_tab	*pt ;
	struct fddi_mib_m *mib_m = NULL;
	struct fddi_mib_p *mib_p = NULL;
	int		len ;
	int		plen ;
	char		*from ;
	char		*to ;
	const char	*swap ;
	char		c ;
	int		range ;
	char		*mib_addr ;
	int		mac ;
	int		path ;
	int		port ;
	int		sp_len ;

	/*
	 * skip if errror
	 */
	if (pcon->pc_err)
		return ;

	/*
	 * actions don't have a value
	 */
	pt = smt_get_ptab(para) ;
	if (pt && pt->p_access == AC_S)
		return ;

	to = (char *) (pcon->pc_p) ;	/* destination pointer */
	len = pcon->pc_len ;		/* free space */
	plen = len ;			/* remember start length */
	pa = (struct smt_para *) to ;	/* type/length pointer */
	to += PARA_LEN ;		/* skip smt_para */
	len -= PARA_LEN ;
	/*
	 * set index if required
	 */
	if (((range = (para & 0xf000)) == 0x2000) ||
		range == 0x3000 || range == 0x4000) {
		if (len < 4)
			goto wrong_error ;
		to[0] = 0 ;
		to[1] = 0 ;
		to[2] = 0 ;
		to[3] = index ;
		len -= 4 ;
		to += 4 ;
	}
	mac = index - INDEX_MAC ;
	path = index - INDEX_PATH ;
	port = index - INDEX_PORT ;
	/*
	 * get pointer to mib
	 */
	switch (range) {
	case 0x1000 :
	default :
		mib_addr = (char *) (&smc->mib) ;
		break ;
	case 0x2000 :
		if (mac < 0 || mac >= NUMMACS) {
			pcon->pc_err = SMT_RDF_NOPARAM ;
			return ;
		}
		mib_addr = (char *) (&smc->mib.m[mac]) ;
		mib_m = (struct fddi_mib_m *) mib_addr ;
		break ;
	case 0x3000 :
		if (path < 0 || path >= NUMPATHS) {
			pcon->pc_err = SMT_RDF_NOPARAM ;
			return ;
		}
		mib_addr = (char *) (&smc->mib.a[path]) ;
		break ;
	case 0x4000 :
		if (port < 0 || port >= smt_mib_phys(smc)) {
			pcon->pc_err = SMT_RDF_NOPARAM ;
			return ;
		}
		mib_addr = (char *) (&smc->mib.p[port_to_mib(smc,port)]) ;
		mib_p = (struct fddi_mib_p *) mib_addr ;
		break ;
	}
	/*
	 * check special paras
	 */
	swap = NULL;
	switch (para) {
	case SMT_P10F0 :
	case SMT_P10F1 :
#ifdef	ESS
	case SMT_P10F2 :
	case SMT_P10F3 :
	case SMT_P10F4 :
	case SMT_P10F5 :
	case SMT_P10F6 :
	case SMT_P10F7 :
#endif
#ifdef	SBA
	case SMT_P10F8 :
	case SMT_P10F9 :
#endif
	case SMT_P20F1 :
		if (!local) {
			pcon->pc_err = SMT_RDF_NOPARAM ;
			return ;
		}
		break ;
	case SMT_P2034 :
	case SMT_P2046 :
	case SMT_P2047 :
	case SMT_P204A :
	case SMT_P2051 :
	case SMT_P2052 :
		mac_update_counter(smc) ;
		break ;
	case SMT_P4022:
		mib_p->fddiPORTPC_LS = LS2MIB(
			sm_pm_get_ls(smc,port_to_mib(smc,port))) ;
		break ;
	case SMT_P_REASON :
		* (u_long *) to = 0 ;
		sp_len = 4 ;
		goto sp_done ;
	case SMT_P1033 :			/* time stamp */
		smt_set_timestamp(smc,smc->mib.fddiSMTTimeStamp) ;
		break ;

	case SMT_P1020:				/* port indexes */
#if	NUMPHYS == 12
		swap = "IIIIIIIIIIII" ;
#else
#if	NUMPHYS == 2
		if (smc->s.sas == SMT_SAS)
			swap = "I" ;
		else
			swap = "II" ;
#else
#if	NUMPHYS == 24
		swap = "IIIIIIIIIIIIIIIIIIIIIIII" ;
#else
	????
#endif
#endif
#endif
		break ;
	case SMT_P3212 :
		{
			sp_len = cem_build_path(smc,to,path) ;
			goto sp_done ;
		}
	case SMT_P1048 :		/* peer wrap condition */
		{
			struct smt_p_1048	*sp ;
			sp = (struct smt_p_1048 *) to ;
			sp->p1048_flag = smc->mib.fddiSMTPeerWrapFlag ;
			sp->p1048_cf_state = smc->mib.fddiSMTCF_State ;
			sp_len = sizeof(struct smt_p_1048) ;
			goto sp_done ;
		}
	case SMT_P208C :
		{
			struct smt_p_208c	*sp ;
			sp = (struct smt_p_208c *) to ;
			sp->p208c_flag =
				smc->mib.m[MAC0].fddiMACDuplicateAddressCond ;
			sp->p208c_dupcondition =
				(mib_m->fddiMACDA_Flag ? SMT_ST_MY_DUPA : 0) |
				(mib_m->fddiMACUNDA_Flag ? SMT_ST_UNA_DUPA : 0);
			sp->p208c_fddilong =
				mib_m->fddiMACSMTAddress ;
			sp->p208c_fddiunalong =
				mib_m->fddiMACUpstreamNbr ;
			sp->p208c_pad = 0 ;
			sp_len = sizeof(struct smt_p_208c) ;
			goto sp_done ;
		}
	case SMT_P208D :		/* frame error condition */
		{
			struct smt_p_208d	*sp ;
			sp = (struct smt_p_208d *) to ;
			sp->p208d_flag =
				mib_m->fddiMACFrameErrorFlag ;
			sp->p208d_frame_ct =
				mib_m->fddiMACFrame_Ct ;
			sp->p208d_error_ct =
				mib_m->fddiMACError_Ct ;
			sp->p208d_lost_ct =
				mib_m->fddiMACLost_Ct ;
			sp->p208d_ratio =
				mib_m->fddiMACFrameErrorRatio ;
			sp_len = sizeof(struct smt_p_208d) ;
			goto sp_done ;
		}
	case SMT_P208E :		/* not copied condition */
		{
			struct smt_p_208e	*sp ;
			sp = (struct smt_p_208e *) to ;
			sp->p208e_flag =
				mib_m->fddiMACNotCopiedFlag ;
			sp->p208e_not_copied =
				mib_m->fddiMACNotCopied_Ct ;
			sp->p208e_copied =
				mib_m->fddiMACCopied_Ct ;
			sp->p208e_not_copied_ratio =
				mib_m->fddiMACNotCopiedRatio ;
			sp_len = sizeof(struct smt_p_208e) ;
			goto sp_done ;
		}
	case SMT_P208F :	/* neighbor change event */
		{
			struct smt_p_208f	*sp ;
			sp = (struct smt_p_208f *) to ;
			sp->p208f_multiple =
				mib_m->fddiMACMultiple_N ;
			sp->p208f_nacondition =
				mib_m->fddiMACDuplicateAddressCond ;
			sp->p208f_old_una =
				mib_m->fddiMACOldUpstreamNbr ;
			sp->p208f_new_una =
				mib_m->fddiMACUpstreamNbr ;
			sp->p208f_old_dna =
				mib_m->fddiMACOldDownstreamNbr ;
			sp->p208f_new_dna =
				mib_m->fddiMACDownstreamNbr ;
			sp->p208f_curren_path =
				mib_m->fddiMACCurrentPath ;
			sp->p208f_smt_address =
				mib_m->fddiMACSMTAddress ;
			sp_len = sizeof(struct smt_p_208f) ;
			goto sp_done ;
		}
	case SMT_P2090 :
		{
			struct smt_p_2090	*sp ;
			sp = (struct smt_p_2090 *) to ;
			sp->p2090_multiple =
				mib_m->fddiMACMultiple_P ;
			sp->p2090_availablepaths =
				mib_m->fddiMACAvailablePaths ;
			sp->p2090_currentpath =
				mib_m->fddiMACCurrentPath ;
			sp->p2090_requestedpaths =
				mib_m->fddiMACRequestedPaths ;
			sp_len = sizeof(struct smt_p_2090) ;
			goto sp_done ;
		}
	case SMT_P4050 :
		{
			struct smt_p_4050	*sp ;
			sp = (struct smt_p_4050 *) to ;
			sp->p4050_flag =
				mib_p->fddiPORTLerFlag ;
			sp->p4050_pad = 0 ;
			sp->p4050_cutoff =
				mib_p->fddiPORTLer_Cutoff ; ;
			sp->p4050_alarm =
				mib_p->fddiPORTLer_Alarm ; ;
			sp->p4050_estimate =
				mib_p->fddiPORTLer_Estimate ;
			sp->p4050_reject_ct =
				mib_p->fddiPORTLem_Reject_Ct ;
			sp->p4050_ct =
				mib_p->fddiPORTLem_Ct ;
			sp_len = sizeof(struct smt_p_4050) ;
			goto sp_done ;
		}

	case SMT_P4051 :
		{
			struct smt_p_4051	*sp ;
			sp = (struct smt_p_4051 *) to ;
			sp->p4051_multiple =
				mib_p->fddiPORTMultiple_U ;
			sp->p4051_porttype =
				mib_p->fddiPORTMy_Type ;
			sp->p4051_connectstate =
				mib_p->fddiPORTConnectState ; ;
			sp->p4051_pc_neighbor =
				mib_p->fddiPORTNeighborType ;
			sp->p4051_pc_withhold =
				mib_p->fddiPORTPC_Withhold ;
			sp_len = sizeof(struct smt_p_4051) ;
			goto sp_done ;
		}
	case SMT_P4052 :
		{
			struct smt_p_4052	*sp ;
			sp = (struct smt_p_4052 *) to ;
			sp->p4052_flag =
				mib_p->fddiPORTEB_Condition ;
			sp->p4052_eberrorcount =
				mib_p->fddiPORTEBError_Ct ;
			sp_len = sizeof(struct smt_p_4052) ;
			goto sp_done ;
		}
	case SMT_P4053 :
		{
			struct smt_p_4053	*sp ;
			sp = (struct smt_p_4053 *) to ;
			sp->p4053_multiple =
				mib_p->fddiPORTMultiple_P ; ;
			sp->p4053_availablepaths =
				mib_p->fddiPORTAvailablePaths ;
			sp->p4053_currentpath =
				mib_p->fddiPORTCurrentPath ;
			memcpy(	(char *) &sp->p4053_requestedpaths,
				(char *) mib_p->fddiPORTRequestedPaths,4) ;
			sp->p4053_mytype =
				mib_p->fddiPORTMy_Type ;
			sp->p4053_neighbortype =
				mib_p->fddiPORTNeighborType ;
			sp_len = sizeof(struct smt_p_4053) ;
			goto sp_done ;
		}
	default :
		break ;
	}
	/*
	 * in table ?
	 */
	if (!pt) {
		pcon->pc_err = (para & 0xff00) ? SMT_RDF_NOPARAM :
						SMT_RDF_ILLEGAL ;
		return ;
	}
	/*
	 * check access rights
	 */
	switch (pt->p_access) {
	case AC_G :
	case AC_GR :
		break ;
	default :
		pcon->pc_err = SMT_RDF_ILLEGAL ;
		return ;
	}
	from = mib_addr + pt->p_offset ;
	if (!swap)
		swap = pt->p_swap ;		/* pointer to swap string */

	/*
	 * copy values
	 */
	while ((c = *swap++)) {
		switch(c) {
		case 'b' :
		case 'w' :
		case 'l' :
			break ;
		case 'S' :
		case 'E' :
		case 'R' :
		case 'r' :
			if (len < 4)
				goto len_error ;
			to[0] = 0 ;
			to[1] = 0 ;
#ifdef	LITTLE_ENDIAN
			if (c == 'r') {
				to[2] = *from++ ;
				to[3] = *from++ ;
			}
			else {
				to[3] = *from++ ;
				to[2] = *from++ ;
			}
#else
			to[2] = *from++ ;
			to[3] = *from++ ;
#endif
			to += 4 ;
			len -= 4 ;
			break ;
		case 'I' :		/* for SET of port indexes */
			if (len < 2)
				goto len_error ;
#ifdef	LITTLE_ENDIAN
			to[1] = *from++ ;
			to[0] = *from++ ;
#else
			to[0] = *from++ ;
			to[1] = *from++ ;
#endif
			to += 2 ;
			len -= 2 ;
			break ;
		case 'F' :
		case 'B' :
			if (len < 4)
				goto len_error ;
			len -= 4 ;
			to[0] = 0 ;
			to[1] = 0 ;
			to[2] = 0 ;
			to[3] = *from++ ;
			to += 4 ;
			break ;
		case 'C' :
		case 'T' :
		case 'L' :
			if (len < 4)
				goto len_error ;
#ifdef	LITTLE_ENDIAN
			to[3] = *from++ ;
			to[2] = *from++ ;
			to[1] = *from++ ;
			to[0] = *from++ ;
#else
			to[0] = *from++ ;
			to[1] = *from++ ;
			to[2] = *from++ ;
			to[3] = *from++ ;
#endif
			len -= 4 ;
			to += 4 ;
			break ;
		case '2' :		/* PortMacIndicated */
			if (len < 4)
				goto len_error ;
			to[0] = 0 ;
			to[1] = 0 ;
			to[2] = *from++ ;
			to[3] = *from++ ;
			len -= 4 ;
			to += 4 ;
			break ;
		case '4' :
			if (len < 4)
				goto len_error ;
			to[0] = *from++ ;
			to[1] = *from++ ;
			to[2] = *from++ ;
			to[3] = *from++ ;
			len -= 4 ;
			to += 4 ;
			break ;
		case 'A' :
			if (len < 8)
				goto len_error ;
			to[0] = 0 ;
			to[1] = 0 ;
			memcpy((char *) to+2,(char *) from,6) ;
			to += 8 ;
			from += 8 ;
			len -= 8 ;
			break ;
		case '8' :
			if (len < 8)
				goto len_error ;
			memcpy((char *) to,(char *) from,8) ;
			to += 8 ;
			from += 8 ;
			len -= 8 ;
			break ;
		case 'D' :
			if (len < 32)
				goto len_error ;
			memcpy((char *) to,(char *) from,32) ;
			to += 32 ;
			from += 32 ;
			len -= 32 ;
			break ;
		case 'P' :		/* timestamp is NOT swapped */
			if (len < 8)
				goto len_error ;
			to[0] = *from++ ;
			to[1] = *from++ ;
			to[2] = *from++ ;
			to[3] = *from++ ;
			to[4] = *from++ ;
			to[5] = *from++ ;
			to[6] = *from++ ;
			to[7] = *from++ ;
			to += 8 ;
			len -= 8 ;
			break ;
		default :
			SMT_PANIC(smc,SMT_E0119, SMT_E0119_MSG) ;
			break ;
		}
	}

done:
	/*
	 * make it even (in case of 'I' encoding)
	 * note: len is DECREMENTED
	 */
	if (len & 3) {
		to[0] = 0 ;
		to[1] = 0 ;
		to += 4 - (len & 3 ) ;
		len = len & ~ 3 ;
	}

	/* set type and length */
	pa->p_type = para ;
	pa->p_len = plen - len - PARA_LEN ;
	/* return values */
	pcon->pc_p = (void *) to ;
	pcon->pc_len = len ;
	return ;

sp_done:
	len -= sp_len ;
	to += sp_len ;
	goto done ;

len_error:
	/* parameter does not fit in frame */
	pcon->pc_err = SMT_RDF_TOOLONG ;
	return ;

wrong_error:
	pcon->pc_err = SMT_RDF_LENGTH ;
}

/*
 * set parameter
 */
static int smt_set_para(struct s_smc *smc, struct smt_para *pa, int index,
			int local, int set)
{
#define IFSET(x)	if (set) (x)

	const struct s_p_tab	*pt ;
	int		len ;
	char		*from ;
	char		*to ;
	const char	*swap ;
	char		c ;
	char		*mib_addr ;
	struct fddi_mib	*mib ;
	struct fddi_mib_m	*mib_m = NULL;
	struct fddi_mib_a	*mib_a = NULL;
	struct fddi_mib_p	*mib_p = NULL;
	int		mac ;
	int		path ;
	int		port ;
	SK_LOC_DECL(u_char,byte_val) ;
	SK_LOC_DECL(u_short,word_val) ;
	SK_LOC_DECL(u_long,long_val) ;

	mac = index - INDEX_MAC ;
	path = index - INDEX_PATH ;
	port = index - INDEX_PORT ;
	len = pa->p_len ;
	from = (char *) (pa + 1 ) ;

	mib = &smc->mib ;
	switch (pa->p_type & 0xf000) {
	case 0x1000 :
	default :
		mib_addr = (char *) mib ;
		break ;
	case 0x2000 :
		if (mac < 0 || mac >= NUMMACS) {
			return(SMT_RDF_NOPARAM) ;
		}
		mib_m = &smc->mib.m[mac] ;
		mib_addr = (char *) mib_m ;
		from += 4 ;		/* skip index */
		len -= 4 ;
		break ;
	case 0x3000 :
		if (path < 0 || path >= NUMPATHS) {
			return(SMT_RDF_NOPARAM) ;
		}
		mib_a = &smc->mib.a[path] ;
		mib_addr = (char *) mib_a ;
		from += 4 ;		/* skip index */
		len -= 4 ;
		break ;
	case 0x4000 :
		if (port < 0 || port >= smt_mib_phys(smc)) {
			return(SMT_RDF_NOPARAM) ;
		}
		mib_p = &smc->mib.p[port_to_mib(smc,port)] ;
		mib_addr = (char *) mib_p ;
		from += 4 ;		/* skip index */
		len -= 4 ;
		break ;
	}
	switch (pa->p_type) {
	case SMT_P10F0 :
	case SMT_P10F1 :
#ifdef	ESS
	case SMT_P10F2 :
	case SMT_P10F3 :
	case SMT_P10F4 :
	case SMT_P10F5 :
	case SMT_P10F6 :
	case SMT_P10F7 :
#endif
#ifdef	SBA
	case SMT_P10F8 :
	case SMT_P10F9 :
#endif
	case SMT_P20F1 :
		if (!local) {
			return(SMT_RDF_NOPARAM) ;
		}
		break ;
	}
	pt = smt_get_ptab(pa->p_type) ;
	if (!pt) {
		return( (pa->p_type & 0xff00) ? SMT_RDF_NOPARAM :
						SMT_RDF_ILLEGAL ) ;
	}
	switch (pt->p_access) {
	case AC_GR :
	case AC_S :
		break ;
	default :
		return(SMT_RDF_ILLEGAL) ;
	}
	to = mib_addr + pt->p_offset ;
	swap = pt->p_swap ;		/* pointer to swap string */

	while (swap && (c = *swap++)) {
		switch(c) {
		case 'b' :
			to = (char *) &byte_val ;
			break ;
		case 'w' :
			to = (char *) &word_val ;
			break ;
		case 'l' :
			to = (char *) &long_val ;
			break ;
		case 'S' :
		case 'E' :
		case 'R' :
		case 'r' :
			if (len < 4) {
				goto len_error ;
			}
			if (from[0] | from[1])
				goto val_error ;
#ifdef	LITTLE_ENDIAN
			if (c == 'r') {
				to[0] = from[2] ;
				to[1] = from[3] ;
			}
			else {
				to[1] = from[2] ;
				to[0] = from[3] ;
			}
#else
			to[0] = from[2] ;
			to[1] = from[3] ;
#endif
			from += 4 ;
			to += 2 ;
			len -= 4 ;
			break ;
		case 'F' :
		case 'B' :
			if (len < 4) {
				goto len_error ;
			}
			if (from[0] | from[1] | from[2])
				goto val_error ;
			to[0] = from[3] ;
			len -= 4 ;
			from += 4 ;
			to += 4 ;
			break ;
		case 'C' :
		case 'T' :
		case 'L' :
			if (len < 4) {
				goto len_error ;
			}
#ifdef	LITTLE_ENDIAN
			to[3] = *from++ ;
			to[2] = *from++ ;
			to[1] = *from++ ;
			to[0] = *from++ ;
#else
			to[0] = *from++ ;
			to[1] = *from++ ;
			to[2] = *from++ ;
			to[3] = *from++ ;
#endif
			len -= 4 ;
			to += 4 ;
			break ;
		case 'A' :
			if (len < 8)
				goto len_error ;
			if (set)
				memcpy((char *) to,(char *) from+2,6) ;
			to += 8 ;
			from += 8 ;
			len -= 8 ;
			break ;
		case '4' :
			if (len < 4)
				goto len_error ;
			if (set)
				memcpy((char *) to,(char *) from,4) ;
			to += 4 ;
			from += 4 ;
			len -= 4 ;
			break ;
		case '8' :
			if (len < 8)
				goto len_error ;
			if (set)
				memcpy((char *) to,(char *) from,8) ;
			to += 8 ;
			from += 8 ;
			len -= 8 ;
			break ;
		case 'D' :
			if (len < 32)
				goto len_error ;
			if (set)
				memcpy((char *) to,(char *) from,32) ;
			to += 32 ;
			from += 32 ;
			len -= 32 ;
			break ;
		case 'P' :		/* timestamp is NOT swapped */
			if (set) {
				to[0] = *from++ ;
				to[1] = *from++ ;
				to[2] = *from++ ;
				to[3] = *from++ ;
				to[4] = *from++ ;
				to[5] = *from++ ;
				to[6] = *from++ ;
				to[7] = *from++ ;
			}
			to += 8 ;
			len -= 8 ;
			break ;
		default :
			SMT_PANIC(smc,SMT_E0120, SMT_E0120_MSG) ;
			return(SMT_RDF_ILLEGAL) ;
		}
	}
	/*
	 * actions and internal updates
	 */
	switch (pa->p_type) {
	case SMT_P101A:			/* fddiSMTConfigPolicy */
		if (word_val & ~1)
			goto val_error ;
		IFSET(mib->fddiSMTConfigPolicy = word_val) ;
		break ;
	case SMT_P101B :		/* fddiSMTConnectionPolicy */
		if (!(word_val & POLICY_MM))
			goto val_error ;
		IFSET(mib->fddiSMTConnectionPolicy = word_val) ;
		break ;
	case SMT_P101D : 		/* fddiSMTTT_Notify */
		if (word_val < 2 || word_val > 30)
			goto val_error ;
		IFSET(mib->fddiSMTTT_Notify = word_val) ;
		break ;
	case SMT_P101E :		/* fddiSMTStatRptPolicy */
		if (byte_val & ~1)
			goto val_error ;
		IFSET(mib->fddiSMTStatRptPolicy = byte_val) ;
		break ;
	case SMT_P101F :		/* fddiSMTTrace_MaxExpiration */
		/*
		 * note: lower limit trace_max = 6.001773... s
		 * NO upper limit
		 */
		if (long_val < (long)0x478bf51L)
			goto val_error ;
		IFSET(mib->fddiSMTTrace_MaxExpiration = long_val) ;
		break ;
#ifdef	ESS
	case SMT_P10F2 :		/* fddiESSPayload */
		if (long_val > 1562)
			goto val_error ;
		if (set && smc->mib.fddiESSPayload != long_val) {
			smc->ess.raf_act_timer_poll = TRUE ;
			smc->mib.fddiESSPayload = long_val ;
		}
		break ;
	case SMT_P10F3 :		/* fddiESSOverhead */
		if (long_val < 50 || long_val > 5000)
			goto val_error ;
		if (set && smc->mib.fddiESSPayload &&
			smc->mib.fddiESSOverhead != long_val) {
			smc->ess.raf_act_timer_poll = TRUE ;
			smc->mib.fddiESSOverhead = long_val ;
		}
		break ;
	case SMT_P10F4 :		/* fddiESSMaxTNeg */
		if (long_val > -MS2BCLK(5) || long_val < -MS2BCLK(165))
			goto val_error ;
		IFSET(mib->fddiESSMaxTNeg = long_val) ;
		break ;
	case SMT_P10F5 :		/* fddiESSMinSegmentSize */
		if (long_val < 1 || long_val > 4478)
			goto val_error ;
		IFSET(mib->fddiESSMinSegmentSize = long_val) ;
		break ;
	case SMT_P10F6 :		/* fddiESSCategory */
		if ((long_val & 0xffff) != 1)
			goto val_error ;
		IFSET(mib->fddiESSCategory = long_val) ;
		break ;
	case SMT_P10F7 :		/* fddiESSSyncTxMode */
		if (word_val > 1)
			goto val_error ;
		IFSET(mib->fddiESSSynchTxMode = word_val) ;
		break ;
#endif
#ifdef	SBA
	case SMT_P10F8 :		/* fddiSBACommand */
		if (byte_val != SB_STOP && byte_val != SB_START)
			goto val_error ;
		IFSET(mib->fddiSBACommand = byte_val) ;
		break ;
	case SMT_P10F9 :		/* fddiSBAAvailable */
		if (byte_val > 100)
			goto val_error ;
		IFSET(mib->fddiSBAAvailable = byte_val) ;
		break ;
#endif
	case SMT_P2020 :		/* fddiMACRequestedPaths */
		if ((word_val & (MIB_P_PATH_PRIM_PREFER |
			MIB_P_PATH_PRIM_ALTER)) == 0 )
			goto val_error ;
		IFSET(mib_m->fddiMACRequestedPaths = word_val) ;
		break ;
	case SMT_P205F :		/* fddiMACFrameErrorThreshold */
		/* 0 .. ffff acceptable */
		IFSET(mib_m->fddiMACFrameErrorThreshold = word_val) ;
		break ;
	case SMT_P2067 :		/* fddiMACNotCopiedThreshold */
		/* 0 .. ffff acceptable */
		IFSET(mib_m->fddiMACNotCopiedThreshold = word_val) ;
		break ;
	case SMT_P2076:			/* fddiMACMA_UnitdataEnable */
		if (byte_val & ~1)
			goto val_error ;
		if (set) {
			mib_m->fddiMACMA_UnitdataEnable = byte_val ;
			queue_event(smc,EVENT_RMT,RM_ENABLE_FLAG) ;
		}
		break ;
	case SMT_P20F1 :		/* fddiMACT_Min */
		IFSET(mib_m->fddiMACT_Min = long_val) ;
		break ;
	case SMT_P320F :
		if (long_val > 1562)
			goto val_error ;
		IFSET(mib_a->fddiPATHSbaPayload = long_val) ;
#ifdef	ESS
		if (set)
			ess_para_change(smc) ;
#endif
		break ;
	case SMT_P3210 :
		if (long_val > 5000)
			goto val_error ;
		
		if (long_val != 0 && mib_a->fddiPATHSbaPayload == 0)
			goto val_error ;

		IFSET(mib_a->fddiPATHSbaOverhead = long_val) ;
#ifdef	ESS
		if (set)
			ess_para_change(smc) ;
#endif
		break ;
	case SMT_P3213:			/* fddiPATHT_Rmode */
		/* no limit :
		 * 0 .. 343.597 => 0 .. 2e32 * 80nS
		 */
		if (set) {
			mib_a->fddiPATHT_Rmode = long_val ;
			rtm_set_timer(smc) ;
		}
		break ;
	case SMT_P3214 :		/* fddiPATHSbaAvailable */
		if (long_val > 0x00BEBC20L)
			goto val_error ;
#ifdef SBA 
		if (set && mib->fddiSBACommand == SB_STOP)
			goto val_error ;
#endif
		IFSET(mib_a->fddiPATHSbaAvailable = long_val) ;
		break ;
	case SMT_P3215 :		/* fddiPATHTVXLowerBound */
		IFSET(mib_a->fddiPATHTVXLowerBound = long_val) ;
		goto change_mac_para ;
	case SMT_P3216 :		/* fddiPATHT_MaxLowerBound */
		IFSET(mib_a->fddiPATHT_MaxLowerBound = long_val) ;
		goto change_mac_para ;
	case SMT_P3217 :		/* fddiPATHMaxT_Req */
		IFSET(mib_a->fddiPATHMaxT_Req = long_val) ;

change_mac_para:
		if (set && smt_set_mac_opvalues(smc)) {
			RS_SET(smc,RS_EVENT) ;
			smc->sm.please_reconnect = 1 ;
			queue_event(smc,EVENT_ECM,EC_DISCONNECT) ;
		}
		break ;
	case SMT_P400E :		/* fddiPORTConnectionPolicies */
		if (byte_val > 1)
			goto val_error ;
		IFSET(mib_p->fddiPORTConnectionPolicies = byte_val) ;
		break ;
	case SMT_P4011 :		/* fddiPORTRequestedPaths */
		/* all 3*8 bits allowed */
		IFSET(memcpy((char *)mib_p->fddiPORTRequestedPaths,
			(char *)&long_val,4)) ;
		break ;
	case SMT_P401F:			/* fddiPORTMaint_LS */
		if (word_val > 4)
			goto val_error ;
		IFSET(mib_p->fddiPORTMaint_LS = word_val) ;
		break ;
	case SMT_P403A :		/* fddiPORTLer_Cutoff */
		if (byte_val < 4 || byte_val > 15)
			goto val_error ;
		IFSET(mib_p->fddiPORTLer_Cutoff = byte_val) ;
		break ;
	case SMT_P403B :		/* fddiPORTLer_Alarm */
		if (byte_val < 4 || byte_val > 15)
			goto val_error ;
		IFSET(mib_p->fddiPORTLer_Alarm = byte_val) ;
		break ;

	/*
	 * Actions
	 */
	case SMT_P103C :		/* fddiSMTStationAction */
		if (smt_action(smc,SMT_STATION_ACTION, (int) word_val, 0))
			goto val_error ;
		break ;
	case SMT_P4046:			/* fddiPORTAction */
		if (smt_action(smc,SMT_PORT_ACTION, (int) word_val,
			port_to_mib(smc,port)))
			goto val_error ;
		break ;
	default :
		break ;
	}
	return(0) ;

val_error:
	/* parameter value in frame is out of range */
	return(SMT_RDF_RANGE) ;

len_error:
	/* parameter value in frame is too short */
	return(SMT_RDF_LENGTH) ;

#if	0
no_author_error:
	/* parameter not setable, because the SBA is not active
	 * Please note: we give the return code 'not authorizeed
	 *  because SBA denied is not a valid return code in the
	 * PMF protocol.
	 */
	return(SMT_RDF_AUTHOR) ;
#endif
}

static const struct s_p_tab *smt_get_ptab(u_short para)
{
	const struct s_p_tab	*pt ;
	for (pt = p_tab ; pt->p_num && pt->p_num != para ; pt++)
		;
	return(pt->p_num ? pt : NULL) ;
}

static int smt_mib_phys(struct s_smc *smc)
{
#ifdef	CONCENTRATOR
	SK_UNUSED(smc) ;

	return(NUMPHYS) ;
#else
	if (smc->s.sas == SMT_SAS)
		return(1) ;
	return(NUMPHYS) ;
#endif
}

static int port_to_mib(struct s_smc *smc, int p)
{
#ifdef	CONCENTRATOR
	SK_UNUSED(smc) ;

	return(p) ;
#else
	if (smc->s.sas == SMT_SAS)
		return(PS) ;
	return(p) ;
#endif
}


#ifdef	DEBUG
#ifndef	BOOT
void dump_smt(struct s_smc *smc, struct smt_header *sm, char *text)
{
	int	len ;
	struct smt_para	*pa ;
	char	*c ;
	int	n ;
	int	nn ;
#ifdef	LITTLE_ENDIAN
	int	smtlen ;
#endif

	SK_UNUSED(smc) ;

#ifdef	DEBUG_BRD
	if (smc->debug.d_smtf < 2)
#else
	if (debug.d_smtf < 2)
#endif
		return ;
#ifdef	LITTLE_ENDIAN
	smtlen = sm->smt_len + sizeof(struct smt_header) ;
#endif
	printf("SMT Frame [%s]:\nDA  ",text) ;
	dump_hex((char *) &sm->smt_dest,6) ;
	printf("\tSA ") ;
	dump_hex((char *) &sm->smt_source,6) ;
	printf(" Class %x Type %x Version %x\n",
		sm->smt_class,sm->smt_type,sm->smt_version)  ;
	printf("TID %lx\t\tSID ",sm->smt_tid) ;
	dump_hex((char *) &sm->smt_sid,8) ;
	printf(" LEN %x\n",sm->smt_len) ;

	len = sm->smt_len ;
	pa = (struct smt_para *) (sm + 1) ;
	while (len > 0 ) {
		int	plen ;
#ifdef UNIX
		printf("TYPE %x LEN %x VALUE\t",pa->p_type,pa->p_len) ;
#else
		printf("TYPE %04x LEN %2x VALUE\t",pa->p_type,pa->p_len) ;
#endif
		n = pa->p_len ;
		if ( (n < 0 ) || (n > (int)(len - PARA_LEN))) {
			n = len - PARA_LEN ;
			printf(" BAD LENGTH\n") ;
			break ;
		}
#ifdef	LITTLE_ENDIAN
		smt_swap_para(sm,smtlen,0) ;
#endif
		if (n < 24) {
			dump_hex((char *)(pa+1),(int) n) ;
			printf("\n") ;
		}
		else {
			int	first = 0 ;
			c = (char *)(pa+1) ;
			dump_hex(c,16) ;
			printf("\n") ;
			n -= 16 ;
			c += 16 ;
			while (n > 0) {
				nn = (n > 16) ? 16 : n ;
				if (n > 64) {
					if (first == 0)
						printf("\t\t\t...\n") ;
					first = 1 ;
				}
				else {
					printf("\t\t\t") ;
					dump_hex(c,nn) ;
					printf("\n") ;
				}
				n -= nn ;
				c += 16 ;
			}
		}
#ifdef	LITTLE_ENDIAN
		smt_swap_para(sm,smtlen,1) ;
#endif
		plen = (pa->p_len + PARA_LEN + 3) & ~3 ;
		len -= plen ;
		pa = (struct smt_para *)((char *)pa + plen) ;
	}
	printf("-------------------------------------------------\n\n") ;
}

void dump_hex(char *p, int len)
{
	int	n = 0 ;
	while (len--) {
		n++ ;
#ifdef UNIX
		printf("%x%s",*p++ & 0xff,len ? ( (n & 7) ? " " : "-") : "") ;
#else
		printf("%02x%s",*p++ & 0xff,len ? ( (n & 7) ? " " : "-") : "") ;
#endif
	}
}
#endif	/* no BOOT */
#endif	/* DEBUG */


#endif	/* no SLIM_SMT */
